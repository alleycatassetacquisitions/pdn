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
#include "wireless/remote-player-manager.hpp"
#include "game/match-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "game/quickdraw-resources.hpp"

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

// Core game objects (declare as pointers, construct in setup())
Device* pdn = nullptr;
PlayerProvider* playerProvider = nullptr;

// Shared game managers, and the apps the device swaps between
GameSession* gameSession = nullptr;
PlayerFetchApp* playerFetchApp = nullptr;
HubApp* hubApp = nullptr;
MatchApp* matchApp = nullptr;
ShootoutApp* shootoutApp = nullptr;
SymbolApp* symbolApp = nullptr;

// Remote player management
SymbolWirelessManager* symbolWirelessManager = nullptr;
RemoteDebugManager* remoteDebugManager = nullptr;

void setupEspNow(RemoteDebugManager* remoteDebugManager,
                 PeerCommsInterface* peerCommsDriver) {
    // Register packet handlers
    peerCommsDriver->setPacketHandler(
        PktType::kDebugPacket,
        [](const uint8_t* srcAddr, const uint8_t* data, const size_t len, void* userArg) {
            ((RemoteDebugManager*)userArg)->ProcessDebugPacket(srcAddr, data, len);
        },
        remoteDebugManager);
}

static void setup() {
    const uint32_t crashedBootUptimeMs = CrashLogger::takeCrashedBootUptime();
    // Before any driver, because two of them need it and neither can bring it
    // up for the other: esp_wifi_init() fails with ESP_ERR_NVS_NOT_INITIALIZED
    // and the preferences driver cannot open a namespace.
    esp_err_t nvsStatus = nvs_flash_init();
    if (nvsStatus == ESP_ERR_NVS_NO_FREE_PAGES || nvsStatus == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvsStatus = nvs_flash_init();
    }
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
    storageDriver = new Esp32S3PrefsDriver(STORAGE_DRIVER_NAME, {MATCHES_PREFS_NAMESPACE, CRASH_LOG_NAMESPACE});

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
    pdn->begin();
    // Create wireless managers
    LOG_I("SETUP", "Creating SymbolWirelessManager...");
    symbolWirelessManager = new SymbolWirelessManager(pdn->getWirelessManager(),
                                                      pdn->getRemoteDeviceCoordinator());
    LOG_I("SETUP", "Creating RemoteDebugManager...");
    remoteDebugManager = new RemoteDebugManager(peerCommsDriver);

    // Register ESP-NOW packet handlers
    setupEspNow(remoteDebugManager, peerCommsDriver);

    crashLogger = new CrashLogger(storageDriver, peerCommsDriver);
    crashLogger->capture(crashedBootUptimeMs);
    crashLogger->transmitPending();

    gameSession = new GameSession(playerProvider->getPlayer(), pdn, symbolWirelessManager);

    GameContext gameContext = gameSession->getContext();
    playerFetchApp = new PlayerFetchApp(pdn->getWirelessManager(),
                                        pdn->getFirmwareUpdateManager(), playerProvider);
    hubApp = new HubApp(gameContext);
    matchApp = new MatchApp(gameContext);
    shootoutApp = new ShootoutApp(gameContext);
    symbolApp = new SymbolApp(gameContext);

    pdn->getDisplay()
    ->invalidateScreen()
    ->drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))
    ->drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))
    ->render();
    SimpleTimer splashTimer;
    splashTimer.setTimer(SPLASH_HOLD_MS);
    while (!splashTimer.expired()) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

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
        crashLogger->restoreEspNowAfterUploads();
    }
    pdn->loop();
    // After the device loop, so the one offer per boot runs with exec() having
    // pumped the radio at least once: the channel pin is retried from there, and a
    // frame sent before it lands goes out where nobody is listening.
    if (crashLogger != nullptr) crashLogger->transmitPending();
}

extern "C" void app_main() {
    setup();
    for (;;) {
        loop();
    }
}
