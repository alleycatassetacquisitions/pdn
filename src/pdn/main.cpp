#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_partition.h>

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
#include "device/drivers/esp32-s3/store-partition.hpp"
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
// The match ring's own partition. Not in DriverConfig: setup() opens it just
// before the match manager, whose ring stays in storageDriver's partition when it
// does not open.
Esp32S3PrefsDriver* matchRingDriver = nullptr;

// Core game objects (declare as pointers, construct in setup())
Device* pdn = nullptr;
PlayerProvider* playerProvider = nullptr;

// Shared game managers
GameSession* gameSession = nullptr;

SymbolWirelessManager* symbolWirelessManager = nullptr;

// True when the store partition is in the partition table, or, on a deployed
// table that ends before its range, once that range is registered for this
// boot. The registration is refused when a partition of the running table
// overlaps the range or the image's flash size (8 MB, which boot already
// checked the chip holds) does not reach its end. Nothing is erased: a
// registration holds only until restart, so a first one cannot be told from a
// later one, and NVS takes other data left in the range, as from an old table's
// spiffs, for free pages and erases each page when it first uses it.
static bool findOrRegisterStorePartition() {
    if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
                                 STORE_PARTITION_LABEL) != nullptr) {
        return true;
    }
    const esp_err_t status = esp_partition_register_external(
    nullptr, STORE_PARTITION_OFFSET, STORE_PARTITION_SIZE, STORE_PARTITION_LABEL,
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    if (status != ESP_OK) {
        LOG_E("SETUP", "Registering the store partition failed: %s", esp_err_to_name(status));
        return false;
    }
    return true;
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
    // The default partition keeps MATCH_RING_PREFS_NAMESPACE so the ring older
    // firmware stored there can be moved, or kept there when the ring's own
    // partition does not open.
    storageDriver = new Esp32S3PrefsDriver(
    STORAGE_DRIVER_NAME,
    {MATCH_RING_PREFS_NAMESPACE, LEGACY_MATCHES_PREFS_NAMESPACE, CRASH_LOG_NAMESPACE});

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

    pdn = PDN::createPDN(pdnConfig);
    
    IdGenerator::initialize(clockDriver->milliseconds());
    playerProvider = new PlayerProvider();
    pdn->begin();
    LOG_I("SETUP", "Creating SymbolWirelessManager...");
    symbolWirelessManager =
    new SymbolWirelessManager(pdn->getWirelessManager(), pdn->getRemoteDeviceCoordinator());

    crashLogger = new CrashLogger(storageDriver);
    crashLogger->capture(crashedBootUptimeMs);

    // The ring's records move out of the default partition, which an init
    // failure above erases and older firmware's matches may have filled.
    if (findOrRegisterStorePartition()) {
        matchRingDriver = new Esp32S3PrefsDriver(MATCH_RING_DRIVER_NAME, STORE_PARTITION_LABEL,
                                                 {MATCH_RING_PREFS_NAMESPACE});
        if (matchRingDriver->initialize() != 0) {
            delete matchRingDriver;
            matchRingDriver = nullptr;
        }
    }
    if (matchRingDriver == nullptr) {
        LOG_E("SETUP", "The match ring stays in the default partition");
    }
    gameSession = new GameSession(playerProvider->getPlayer(), playerProvider->getRecordKey(), pdn,
                                  symbolWirelessManager, matchRingDriver);
    gameSession->setCrashRecordSource(crashLogger);

    PlayerFetchApp* playerFetchApp =
    new PlayerFetchApp(pdn->getWirelessManager(), pdn->getFirmwareUpdateManager(), playerProvider);

    pdn->getDisplay()
    ->invalidateScreen()
    ->drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))
    ->drawImage(getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))
    ->render();
    vTaskDelay(pdMS_TO_TICKS(SPLASH_HOLD_MS));

    // Register the swappable state machines and boot into registration
    AppConfig apps = makeGameplayApps(gameSession->getContext());
    apps[StateId(PLAYER_FETCH_APP_ID)] = playerFetchApp;
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
