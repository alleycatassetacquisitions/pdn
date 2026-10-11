#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

// How long the splash screen stays up before the first app mounts.
constexpr unsigned long SPLASH_HOLD_MS = 2000;

#include "device/crash/crash-logger.hpp"
#include "device/drivers/esp32-s3/esp32-s3-logger-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-clock-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-1-button-driver.hpp"
#include "device/drivers/esp32-s3/ws2812b-led-strip-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-haptics-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-serial-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-http-client-driver.hpp"
#include "device/drivers/esp32-s3/esp-now-driver.hpp"
#include "device/drivers/esp32-s3/ssd1309-u8g2-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-prefs-driver.hpp"

#include "fdn-constants.hpp"
#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "apps/main-menu/main-menu.hpp"
#include "apps/idle/idle.hpp"
#include "apps/hacking/hacking.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "apps/symbol-match/symbol-match.hpp"
#include "apps/fdn-app-ids.hpp"

// WiFi configuration - injected at compile time from wifi_credentials.ini
// See wifi_credentials.ini.example for template
#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Please create wifi_credentials.ini from wifi_credentials.ini.example"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined. Please create wifi_credentials.ini from wifi_credentials.ini.example"
#endif
#ifndef BASE_URL
#error "BASE_URL not defined. Please create wifi_credentials.ini from wifi_credentials.ini.example"
#endif

WifiConfig* wifiConfig = nullptr;
CrashLogger* crashLogger = nullptr;
// ESP32-S3 Drivers
Esp32S3Clock*    clockDriver              = nullptr;
SSD1309U8G2Driver* displayDriver         = nullptr;
Esp32S31ButtonDriver* primaryButtonDriver   = nullptr;
Esp32S31ButtonDriver* secondaryButtonDriver = nullptr;
Esp32S31ButtonDriver* tertiaryButtonDriver  = nullptr;
// Recess lights on DISPLAY pin slot, fin lights on GRIP pin slot
WS2812BLedStripDriver* lightDriver = nullptr;
Esp32S3HapticsDriver* hapticsDriver      = nullptr;
Esp32s3SerialIn* serialInDriver          = nullptr;
Esp32s3SerialInSecondary* serialInSecondaryDriver = nullptr;
Esp32S3HttpClient* httpClientDriver      = nullptr;
EspNowDriver*   peerCommsDriver         = nullptr;
Esp32S3Logger*   loggerDriver            = nullptr;
Esp32S3PrefsDriver* storageDriver        = nullptr;

// FDN device
FDN* fdn = nullptr;

// Player identity
Player fdnPlayer = FDN_PLAYER;

// Managers
RemotePlayerManager*      remotePlayerManager      = nullptr;
FDNConnectWirelessManager* fdnConnectWirelessManager = nullptr;
HackedPlayersManager*     hackedPlayersManager     = nullptr;
SymbolWirelessManager*    symbolWirelessManager    = nullptr;

// Apps
MainMenu*    mainMenu        = nullptr;
Idle*        idleApp         = nullptr;
Hacking*     hackingApp      = nullptr;
SymbolMatch* symbolMatchApp  = nullptr;

static constexpr unsigned long PLAYER_BROADCAST_INTERVAL_MS = 12000;

static void setupEspNow(PeerCommsInterface* peerComms) {
    peerComms->setPacketHandler(
        PktType::kPlayerInfoBroadcast,
        [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
            static_cast<RemotePlayerManager*>(arg)->ProcessPlayerInfoPkt(src, data, len);
        },
        remotePlayerManager);

    peerComms->setPacketHandler(
        PktType::kFdnConnect,
        [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
            static_cast<FDNConnectWirelessManager*>(arg)->processPacket(src, data, len);
        },
        fdnConnectWirelessManager);

    peerComms->setPacketHandler(
        PktType::kSymbolMatchCommand,
        [](const uint8_t* src, const uint8_t* data, size_t len, void* arg) {
            static_cast<SymbolWirelessManager*>(arg)->processSymbolMatchCommand(src, data, len);
        },
        symbolWirelessManager);
}

static void setup() {
    // Before any driver, because two of them need it and neither can bring it
    // up for the other: esp_wifi_init() fails with ESP_ERR_NVS_NOT_INITIALIZED
    // and the preferences driver cannot open a namespace.
    esp_err_t nvsStatus = nvs_flash_init();
    if (nvsStatus == ESP_ERR_NVS_NO_FREE_PAGES || nvsStatus == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvsStatus = nvs_flash_init();
    }
    if (nvsStatus != ESP_OK) {
        ESP_LOGE("FDN", "nvs_flash_init failed: %s", esp_err_to_name(nvsStatus));
    }

    // Construct platform drivers first — logging and timers depend on these.
    loggerDriver = new Esp32S3Logger(LOGGER_DRIVER_NAME);
    clockDriver  = new Esp32S3Clock(PLATFORM_CLOCK_DRIVER_NAME);

    g_logger = loggerDriver;
    SimpleTimer::setPlatformClock(clockDriver);
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Remaining drivers (safe to log now)
    displayDriver = new SSD1309U8G2Driver(
        DISPLAY_DRIVER_NAME, fdnDisplayCS, fdnDisplayDC, fdnDisplayRST);
    primaryButtonDriver   = new Esp32S31ButtonDriver(PRIMARY_BUTTON_DRIVER_NAME,   fdnPrimaryButtonPin);
    secondaryButtonDriver = new Esp32S31ButtonDriver(SECONDARY_BUTTON_DRIVER_NAME, fdnSecondaryButtonPin);
    tertiaryButtonDriver  = new Esp32S31ButtonDriver(TERTIARY_BUTTON_DRIVER_NAME,  fdnTertiaryButtonPin);
    lightDriver = new WS2812BLedStripDriver(LIGHT_DRIVER_NAME, fdnRecessLightsPin, fdnFinLightsPin,
                                           fdnNumRecessLights, fdnNumFinLights);
    hapticsDriver = new Esp32S3HapticsDriver(HAPTICS_DRIVER_NAME, fdnMotorPin);
    serialInDriver          = new Esp32s3SerialIn(SERIAL_IN_DRIVER_NAME, fdnRXt, fdnRXr);
    serialInSecondaryDriver = new Esp32s3SerialInSecondary(SERIAL_IN_SECONDARY_DRIVER_NAME, fdnRXt2, fdnRXr2);

    wifiConfig    = new WifiConfig(WIFI_SSID, WIFI_PASSWORD, BASE_URL);
    peerCommsDriver = EspNowDriver::CreateEspNowManager(PEER_COMMS_DRIVER_NAME);
    httpClientDriver = new Esp32S3HttpClient(HTTP_CLIENT_DRIVER_NAME, wifiConfig);
    storageDriver = new Esp32S3PrefsDriver(STORAGE_DRIVER_NAME, {FDN_PREF_NAMESPACE, CRASH_LOG_NAMESPACE});

    DriverConfig fdnConfig = {
        {DISPLAY_DRIVER_NAME,              displayDriver},
        {PRIMARY_BUTTON_DRIVER_NAME,       primaryButtonDriver},
        {SECONDARY_BUTTON_DRIVER_NAME,     secondaryButtonDriver},
        {TERTIARY_BUTTON_DRIVER_NAME,      tertiaryButtonDriver},
        {LIGHT_DRIVER_NAME,                lightDriver},
        {HAPTICS_DRIVER_NAME,              hapticsDriver},
        {SERIAL_IN_DRIVER_NAME,            serialInDriver},
        {SERIAL_IN_SECONDARY_DRIVER_NAME,  serialInSecondaryDriver},
        {HTTP_CLIENT_DRIVER_NAME,          httpClientDriver},
        {PEER_COMMS_DRIVER_NAME,           peerCommsDriver},
        {PLATFORM_CLOCK_DRIVER_NAME,       clockDriver},
        {LOGGER_DRIVER_NAME,               loggerDriver},
        {STORAGE_DRIVER_NAME,              storageDriver},
    };

    fdn = FDN::createFDN(fdnConfig);
    fdn->begin();

    // Managers — constructed after fdn->begin() so wireless is ready.
    remotePlayerManager = new RemotePlayerManager(peerCommsDriver);
    remotePlayerManager->StartBroadcastingPlayerInfo(&fdnPlayer, PLAYER_BROADCAST_INTERVAL_MS);

    fdnConnectWirelessManager = new FDNConnectWirelessManager();
    fdnConnectWirelessManager->initialize(fdn->getWirelessManager());

    hackedPlayersManager  = new HackedPlayersManager(fdn->getStorage());

    symbolWirelessManager = new SymbolWirelessManager();
    symbolWirelessManager->initialize(fdn->getWirelessManager(), fdn->getRemoteDeviceCoordinator());

    setupEspNow(peerCommsDriver);

    crashLogger = new CrashLogger(storageDriver, peerCommsDriver);
    crashLogger->capture();
    crashLogger->transmitPending();

    // Apps
    idleApp = new Idle(
        remotePlayerManager, hackedPlayersManager,
        fdnConnectWirelessManager, fdn->getRemoteDeviceCoordinator());

    mainMenu = new MainMenu(fdn, remotePlayerManager);

    hackingApp = new Hacking(
        fdnConnectWirelessManager, hackedPlayersManager,
        fdn->getRemoteDeviceCoordinator());

    symbolMatchApp = new SymbolMatch(
        fdn, symbolWirelessManager,
        remotePlayerManager, hackedPlayersManager,
        fdnConnectWirelessManager);

    // Splash screen
    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("ALLEYCAT", 20, 32)
        ->render();
    vTaskDelay(pdMS_TO_TICKS(SPLASH_HOLD_MS));

    AppConfig apps = {
        {StateId(SYMBOL_MATCH_APP_ID), symbolMatchApp},
        {StateId(MAIN_MENU_APP_ID),    mainMenu},
        {StateId(HACKING_APP_ID),      hackingApp},
        {StateId(IDLE_APP_ID),         idleApp},
    };
    fdn->loadAppConfig(apps, StateId(SYMBOL_MATCH_APP_ID));
}

void loop() {
    if (crashLogger != nullptr) {
        crashLogger->pollSerialCommand();
    }
    fdn->loop();
}

extern "C" void app_main() {
    setup();
    for (;;) {
        loop();
    }
}
