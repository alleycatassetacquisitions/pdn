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
#include "device/drivers/esp32-s3/ssd1306-u8g2-driver.hpp"
#include "device/drivers/esp32-s3/esp32-s3-prefs-driver.hpp"

#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include "state/state-machine.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw.hpp"
#include "game/konami-metagame.hpp"
#include "id-generator.hpp"
#include "game/match-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"

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
SSD1306U8G2Driver* displayDriver = nullptr;
Esp32S31ButtonDriver* primaryButtonDriver = nullptr;
Esp32S31ButtonDriver* secondaryButtonDriver = nullptr;
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
IdGenerator* idGenerator = nullptr;
Player* player = nullptr;

// Game instances
Quickdraw* game = nullptr;
KonamiMetaGame* konamiMetaGame = nullptr;

// Remote player management
QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
RemoteDebugManager* remoteDebugManager = nullptr;

void setupEspNow(QuickdrawWirelessManager* quickdrawWirelessManager, RemoteDebugManager* remoteDebugManager, PeerCommsInterface* peerCommsDriver) {
    // Register packet handlers
    peerCommsDriver->setPacketHandler(
        PktType::kQuickdrawCommand,
        [](const uint8_t* src, const uint8_t* data, const size_t len, void* userArg) {
            ((QuickdrawWirelessManager*)userArg)->processQuickdrawCommand(src, data, len);
        },
        quickdrawWirelessManager
    );
    
    peerCommsDriver->setPacketHandler(
        PktType::kDebugPacket,
        [](const uint8_t* srcAddr, const uint8_t* data, const size_t len, void* userArg) {
            ((RemoteDebugManager*)userArg)->ProcessDebugPacket(srcAddr, data, len);
        },
        remoteDebugManager
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

    // Now construct remaining drivers (safe to use logging and timers now)
    displayDriver = new SSD1306U8G2Driver(DISPLAY_DRIVER_NAME);
    primaryButtonDriver = new Esp32S31ButtonDriver(PRIMARY_BUTTON_DRIVER_NAME, primaryButtonPin);
    secondaryButtonDriver = new Esp32S31ButtonDriver(SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonPin);
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
    
    idGenerator = new IdGenerator(clockDriver->milliseconds());
    idGenerator->seed(clockDriver->milliseconds());
    player = new Player();
    player->setUserID(idGenerator->generateId());
    pdn->begin();
    
    // Create wireless managers
    LOG_I("SETUP", "Creating QuickdrawWirelessManager...");
    quickdrawWirelessManager = new QuickdrawWirelessManager();
    LOG_I("SETUP", "Creating RemoteDebugManager...");
    remoteDebugManager = new RemoteDebugManager(peerCommsDriver);
    
    // WiFi credentials are compile-time constants from build flags
    remoteDebugManager->Initialize(WIFI_SSID, WIFI_PASSWORD, BASE_URL);

    quickdrawWirelessManager->initialize(player, pdn->getWirelessManager(), 1000);
    
    // Register ESP-NOW packet handlers
    setupEspNow(quickdrawWirelessManager, remoteDebugManager, peerCommsDriver);
    
    game = new Quickdraw(player, pdn, quickdrawWirelessManager, remoteDebugManager);
    konamiMetaGame = new KonamiMetaGame(player);

    pdn->getDisplay()->
    invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))->
        render();
    delay(3000);

    // Register state machines with the device and launch Quickdraw
    AppConfig apps = {
        {StateId(QUICKDRAW_APP_ID), game},
        {StateId(KONAMI_METAGAME_APP_ID), konamiMetaGame}
    };
    pdn->loadAppConfig(apps, StateId(QUICKDRAW_APP_ID));
}

void loop() {
    pdn->loop();
}
