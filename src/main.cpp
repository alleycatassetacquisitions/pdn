#include <Arduino.h>

#include <WiFi.h>
#include <FastLED.h>
#include <Preferences.h>

#include "device/drivers/esp32-s3/esp32-s3-logger-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-clock-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-1-button-driver.hpp"
#include "device/drivers/esp32-s3/ws2812b-fastled-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-haptics-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-serial-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-http-client-driver.hpp"
#include "device/drivers/esp32-s3/esp-now-driver.hpp"
#include "device/drivers/esp32-s3/ssd1309-u8g2-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-prefs-driver.hpp"

#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include "state/state-machine.hpp"
#include "device/pdn.hpp"
#include "id-generator.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "apps/main-menu/main-menu.hpp"
#include "apps/main-menu/main-menu-resources.hpp"

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


// ESP32-s3 Drivers (declare as pointers, construct in setup())
Esp32S3Clock* clockDriver = nullptr;
SSD1309U8G2Driver* displayDriver = nullptr;
Esp32S31ButtonDriver* primaryButtonDriver = nullptr;
Esp32S31ButtonDriver* secondaryButtonDriver = nullptr;
Esp32S31ButtonDriver* tertiaryButtonDriver = nullptr;
WS2812BFastLEDDriver* lightDriver = nullptr;
Esp32S3HapticsDriver* hapticsDriver = nullptr;
Esp32s3SerialOut* serialOutDriver = nullptr;
Esp32s3SerialIn* serialInDriver = nullptr;
Esp32S3HttpClient* httpClientDriver = nullptr;
EspNowManager* peerCommsDriver = nullptr;
Esp32S3Logger* loggerDriver = nullptr;
Esp32S3PrefsDriver* storageDriver = nullptr;

// Core game objects (declare as pointers, construct in setup())
Device* pdn = nullptr;
Player* player = nullptr;

// Game instance
MainMenu* mainMenu = nullptr;

// Remote player management
RemotePlayerManager* remotePlayerManager = nullptr;

void setupEspNow(RemotePlayerManager* remotePlayerManager, PeerCommsInterface* peerCommsDriver) {
    // Register packet handlers
    peerCommsDriver->setPacketHandler(
        PktType::kQuickdrawCommand,
        [](const uint8_t* src, const uint8_t* data, const size_t len, void* userArg) {
            ((RemotePlayerManager*)userArg)->ProcessPlayerInfoPkt(src, data, len);
        },
        remotePlayerManager
    );
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    // Construct drivers FIRST (before anything that might use logging or timers)
    loggerDriver = new Esp32S3Logger(LOGGER_DRIVER_NAME);
    clockDriver = new Esp32S3Clock(PLATFORM_CLOCK_DRIVER_NAME);
    
    // Initialize platform abstractions immediately after constructing them
    g_logger = loggerDriver;
    SimpleTimer::setPlatformClock(clockDriver);
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Now construct remaining drivers (safe to use logging and timers now)
    displayDriver = new SSD1309U8G2Driver(DISPLAY_DRIVER_NAME);
    primaryButtonDriver = new Esp32S31ButtonDriver(PRIMARY_BUTTON_DRIVER_NAME, primaryButtonPin);
    secondaryButtonDriver = new Esp32S31ButtonDriver(SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonPin);
    tertiaryButtonDriver = new Esp32S31ButtonDriver(TERTIARY_BUTTON_DRIVER_NAME, tertiaryButtonPin);
    lightDriver = new WS2812BFastLEDDriver(LIGHT_DRIVER_NAME);
    hapticsDriver = new Esp32S3HapticsDriver(HAPTICS_DRIVER_NAME, motorPin);
    serialOutDriver = new Esp32s3SerialOut(SERIAL_OUT_DRIVER_NAME);
    serialInDriver = new Esp32s3SerialIn(SERIAL_IN_DRIVER_NAME);
    
    // WiFi credentials are compile-time constants from build flags
    wifiConfig = new WifiConfig(WIFI_SSID, WIFI_PASSWORD, BASE_URL);
    peerCommsDriver = EspNowManager::CreateEspNowManager(PEER_COMMS_DRIVER_NAME);
    httpClientDriver = new Esp32S3HttpClient(HTTP_CLIENT_DRIVER_NAME, wifiConfig);
    storageDriver = new Esp32S3PrefsDriver(STORAGE_DRIVER_NAME, PREF_NAMESPACE);

    // Create driver configuration
    DriverConfig pdnConfig = {
        {DISPLAY_DRIVER_NAME, displayDriver},
        {PRIMARY_BUTTON_DRIVER_NAME, primaryButtonDriver},
        {SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonDriver},
        {TERTIARY_BUTTON_DRIVER_NAME, tertiaryButtonDriver},
        {LIGHT_DRIVER_NAME, lightDriver},
        {HAPTICS_DRIVER_NAME, hapticsDriver},
        {SERIAL_OUT_DRIVER_NAME, serialOutDriver},
        {SERIAL_IN_DRIVER_NAME, serialInDriver},
        {HTTP_CLIENT_DRIVER_NAME, httpClientDriver},
        {PEER_COMMS_DRIVER_NAME, peerCommsDriver},
        {PLATFORM_CLOCK_DRIVER_NAME, clockDriver},
        {LOGGER_DRIVER_NAME, loggerDriver},
        {STORAGE_DRIVER_NAME, storageDriver},
    };

    // Create core game objects
    pdn = PDN::createPDN(pdnConfig);
    
    IdGenerator::initialize(clockDriver->milliseconds());
    player = new Player();
    player->setUserID(IdGenerator::getInstance().generateId());
    pdn->begin();
    
    // Register ESP-NOW packet handlers
    setupEspNow(remotePlayerManager, peerCommsDriver);
    
    mainMenu = new MainMenu(pdn, remotePlayerManager);
    
    pdn->getDisplay()->
    invalidateScreen()->
        drawImage(alleycatLogoImage)->
        render();
    delay(3000);

    // Register state machines with the device and launch Main Menu
    AppConfig apps = {
        {StateId(MAIN_MENU_APP_ID), mainMenu}
    };
    pdn->loadAppConfig(apps, StateId(MAIN_MENU_APP_ID));
}

void loop() {
    pdn->loop();
}
