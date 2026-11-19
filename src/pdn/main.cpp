#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>

// How long the splash screen stays up before the first app mounts.
constexpr unsigned long SPLASH_HOLD_MS = 3000;

#include "device/drivers/esp32-s3/esp32-s3-logger-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-clock-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-1-button-driver.hpp"
#include "device/drivers/esp32-s3/ws2812b-led-strip-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-haptics-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-serial-driver.hpp"
#include "device/drivers/esp32-s3/esp-now-driver.hpp"
#include "device/drivers/esp32-s3/ssd1306-u8g2-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-prefs-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-firmware-store.hpp"
#include "device/crash/crash-logger.hpp"

#include "pdn-constants.hpp"
#include "utils/simple-timer.hpp"
#include "game/player-provider.hpp"
#include "state/state-machine.hpp"
#include "device/pdn.hpp"
#include "game/game-session.hpp"
#include "game/quickdraw-apps.hpp"
#include "apps/player-fetch/player-fetch.hpp"
#include "id-generator.hpp"
#include "game/match-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#ifdef PDN_TEST_PLAYER
#ifdef PDN_RELEASE_BUILD
#error "PDN_TEST_PLAYER must never reach a release build"
#endif
constexpr bool TEST_PLAYER_IDENTITY = true;
#else
constexpr bool TEST_PLAYER_IDENTITY = false;
#endif

CrashLogger* crashLogger = nullptr;
Esp32S3FirmwareStore* firmwareStore = nullptr;

// ESP32-s3 Drivers (declare as pointers, construct in setup())
Esp32S3Clock* clockDriver = nullptr;
SSD1306U8G2Driver* displayDriver = nullptr;
Esp32S31ButtonDriver* primaryButtonDriver = nullptr;
Esp32S31ButtonDriver* secondaryButtonDriver = nullptr;
WS2812BLedStripDriver* lightDriver = nullptr;
Esp32S3HapticsDriver* hapticsDriver = nullptr;
Esp32s3SerialOut* serialOutDriver = nullptr;
Esp32s3SerialIn* serialInDriver = nullptr;
EspNowDriver* peerCommsDriver = nullptr;
Esp32S3Logger* loggerDriver = nullptr;
Esp32S3PrefsDriver* storageDriver = nullptr;
// The match ring's own partition, at the end of flash after nvs_keys. Deployed
// tables (partitions.csv) end before it, so the driver registers the range at
// runtime. partitions-test.csv lists it; the driver refuses a table that lists
// the label at any other range.
constexpr NvsPartitionRange MATCH_RING_PARTITION = {"records", 0x7E1000, 0x1F000};
// Not in DriverConfig: the match manager opens it, and keeps the ring in
// storageDriver's partition when it does not open.
Esp32S3PrefsDriver* matchRingDriver = nullptr;

// Core game objects (declare as pointers, construct in setup())
Device* pdn = nullptr;
PlayerProvider* playerProvider = nullptr;
BandRegistration* bandRegistration = nullptr;

// Shared game managers, and the apps the device swaps between
GameSession* gameSession = nullptr;
PlayerFetchApp* playerFetchApp = nullptr;
HubApp* hubApp = nullptr;
MatchApp* matchApp = nullptr;
ShootoutApp* shootoutApp = nullptr;
SymbolApp* symbolApp = nullptr;

// Symbol-match wireless traffic
SymbolWirelessManager* symbolWirelessManager = nullptr;

static void setup() {
    const uint32_t crashedBootUptimeMs = CrashLogger::takeCrashedBootUptime();
    // Before any driver, because two of them need it and neither can bring it
    // up for the other: esp_wifi_init() fails with ESP_ERR_NVS_NOT_INITIALIZED
    // and the preferences driver cannot open a namespace.
    // Never erased on failure: that would wipe every stored record. The PDN runs
    // without storage, and so without its radio, until the desk reflashes or clears it.
    esp_err_t nvsStatus = nvs_flash_init();
    if (nvsStatus != ESP_OK) {
        ESP_LOGE("PDN", "nvs_flash_init failed: %s", esp_err_to_name(nvsStatus));
    }

    // Construct drivers FIRST (before anything that might use logging or timers)
    loggerDriver = new Esp32S3Logger(LOGGER_DRIVER_NAME);
    clockDriver = new Esp32S3Clock(PLATFORM_CLOCK_DRIVER_NAME);
    
    // Initialize platform abstractions immediately after constructing them
    g_logger = loggerDriver;
    SimpleTimer::setPlatformClock(clockDriver);
    // Next, so its watchdog covers the rest of setup() on an unconfirmed image.
    firmwareStore = new Esp32S3FirmwareStore(FIRMWARE_STORE_DRIVER_NAME);

    // Now construct remaining drivers (safe to use logging and timers now)
    displayDriver = new SSD1306U8G2Driver(DISPLAY_DRIVER_NAME, displayCS, displayDC, displayRST);
    primaryButtonDriver = new Esp32S31ButtonDriver(PRIMARY_BUTTON_DRIVER_NAME, primaryButtonPin);
    secondaryButtonDriver = new Esp32S31ButtonDriver(SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonPin);
    lightDriver = new WS2812BLedStripDriver(LIGHT_DRIVER_NAME, displayLightsPin, gripLightsPin,
                                           numDisplayLights, numGripLights);
    hapticsDriver = new Esp32S3HapticsDriver(HAPTICS_DRIVER_NAME, motorPin);
    serialOutDriver = new Esp32s3SerialOut(SERIAL_OUT_DRIVER_NAME, TXt, TXr);
    serialInDriver = new Esp32s3SerialIn(SERIAL_IN_DRIVER_NAME, RXt, RXr);

    peerCommsDriver = EspNowDriver::CreateEspNowManager(PEER_COMMS_DRIVER_NAME);
    // storageDriver keeps MATCH_RING_PREFS_NAMESPACE so the ring earlier firmware
    // kept there can move to its own partition, or stay when that does not open.
    storageDriver =
    new Esp32S3PrefsDriver(STORAGE_DRIVER_NAME, {MATCH_RING_PREFS_NAMESPACE, CRASH_LOG_NAMESPACE});
    matchRingDriver = new Esp32S3PrefsDriver(MATCH_RING_DRIVER_NAME, MATCH_RING_PARTITION,
                                             {MATCH_RING_PREFS_NAMESPACE});

    // Create driver configuration
    DriverConfig pdnConfig = {
    {DISPLAY_DRIVER_NAME, displayDriver},
    {PRIMARY_BUTTON_DRIVER_NAME, primaryButtonDriver},
    {SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonDriver},
    {LIGHT_DRIVER_NAME, lightDriver},
    {HAPTICS_DRIVER_NAME, hapticsDriver},
    {SERIAL_OUT_DRIVER_NAME, serialOutDriver},
    {SERIAL_IN_DRIVER_NAME, serialInDriver},
    {PEER_COMMS_DRIVER_NAME, peerCommsDriver},
    {PLATFORM_CLOCK_DRIVER_NAME, clockDriver},
    {LOGGER_DRIVER_NAME, loggerDriver},
    {STORAGE_DRIVER_NAME, storageDriver},
    {FIRMWARE_STORE_DRIVER_NAME, firmwareStore},
    };

    // Create core game objects
    pdn = PDN::createPDN(pdnConfig);
    
    IdGenerator::initialize(clockDriver->milliseconds());
    playerProvider = new PlayerProvider();
    playerProvider->getPlayer()->setUserID(IdGenerator::getInstance().generateId());
    pdn->begin();
    // Create wireless managers
    LOG_I("SETUP", "Creating SymbolWirelessManager...");
    symbolWirelessManager =
    new SymbolWirelessManager(pdn->getWirelessManager(), pdn->getRemoteDeviceCoordinator());

    // The PDN's crash records go to an FDN through the record relay.
    crashLogger = new CrashLogger(storageDriver);
    if (nvsStatus == ESP_OK) crashLogger->capture(crashedBootUptimeMs);  // it erases the dump

    gameSession = new GameSession(playerProvider->getPlayer(), playerProvider->getRecordKey(), pdn,
                                  symbolWirelessManager, matchRingDriver);
    gameSession->setCrashRecordSource(crashLogger);

    GameContext gameContext = gameSession->getContext();
    bandRegistration = new BandRegistration(pdn->getWirelessManager(),
                                            pdn->getRemoteDeviceCoordinator(), playerProvider);
    playerFetchApp =
    new PlayerFetchApp(pdn->getFirmwareUpdateManager(), playerProvider, bandRegistration,
                       pdn->getRemoteDeviceCoordinator(), TEST_PLAYER_IDENTITY);
    hubApp = new HubApp(gameContext);
    matchApp = new MatchApp(gameContext);
    shootoutApp = new ShootoutApp(gameContext);
    symbolApp = new SymbolApp(gameContext);

    pdn->getDisplay()->
    invalidateScreen()->
        drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))->
        drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))->
        render();
    vTaskDelay(pdMS_TO_TICKS(SPLASH_HOLD_MS));

    // Register the swappable state machines and boot into registration
    AppConfig apps = {
    {StateId(PLAYER_FETCH_APP_ID), playerFetchApp},
    {StateId(HUB_APP_ID), hubApp},
    {StateId(MATCH_APP_ID), matchApp},
    {StateId(SHOOTOUT_APP_ID), shootoutApp},
    {StateId(SYMBOL_APP_ID), symbolApp},
    };
    pdn->loadAppConfig(apps, StateId(PLAYER_FETCH_APP_ID));
}

void loop() {
    if (crashLogger != nullptr) {
        CrashLogger::noteUptime();
        crashLogger->pollSerialCommand();
    }
    pdn->loop();
}

extern "C" void app_main() {
    setup();
    for (;;) {
        loop();
    }
}
