#include <Arduino.h>

#include <WiFi.h>
#include <FastLED.h>
#include <Preferences.h>

#include "logger.hpp"

#include "device/drivers/esp32-s3-logger-driver.hpp"
#include "device/drivers/esp32-s3-clock-driver.hpp"
#include "device/drivers/esp32-s3-1-button-driver.hpp"
#include "device/drivers/ws2812b-fastled-driver.hpp"
#include "device/drivers/esp32-s3-haptics-driver.hpp"
#include "device/drivers/esp32-s3-serial-driver.hpp"
#include "device/drivers/esp32-s3-http-client-driver.hpp"
#include "device/drivers/esp-now-driver.hpp"
#include "device/drivers/ssd1306-u8g2-driver.hpp"
#include "device/drivers/esp32-s3-prefs-driver.hpp"

#include "utils/simple-timer.hpp"
#include "player.hpp"
#include "state-machine.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw.hpp"
#include "id-generator.hpp"
#include "wireless/remote-player-manager.hpp"
#include "game/match-manager.hpp"
#include "wireless-types.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "peer-comms-interface.hpp"

// WiFi configuration
const char* WIFI_SSID = "NeoCore Networks";
const char* WIFI_PASSWORD = "AlleyCatBountyHunting1";
const char* BASE_URL = "http://alleycat-server.local:3000";

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

// Game instance
Quickdraw* game = nullptr;

// Remote player management
QuickdrawWirelessManager* quickdrawWirelessManager = nullptr;
RemoteDebugManager* remoteDebugManager = nullptr;

void setupEspNow() {
    // Register packet handlers
    EspNowManager::GetInstance()->setPacketHandler(
        static_cast<uint8_t>(PktType::kQuickdrawCommand),
        [](const PeerCommsInterface::PeerAddress& src, const uint8_t* data, const size_t len, void* userArg) {
            ((QuickdrawWirelessManager*)userArg)->processQuickdrawCommand(src.data(), data, len);
        },
        QuickdrawWirelessManager::GetInstance()
    );
    
    EspNowManager::GetInstance()->setPacketHandler(
        static_cast<uint8_t>(PktType::kDebugPacket),
        [](const PeerCommsInterface::PeerAddress& srcAddr, const uint8_t* data, const size_t len, void* userArg) {
            ((RemoteDebugManager*)userArg)->ProcessDebugPacket(srcAddr.data(), data, len);
        },
        RemoteDebugManager::GetInstance()
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
    
    wifiConfig = new WifiConfig(WIFI_SSID, WIFI_PASSWORD, BASE_URL);
    httpClientDriver = new Esp32S3HttpClient(HTTP_CLIENT_DRIVER_NAME, wifiConfig);
    peerCommsDriver = EspNowManager::CreateEspNowManager(PEER_COMMS_DRIVER_NAME);
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
    
    idGenerator = IdGenerator::GetInstance();
    player = new Player();
    player->setUserID(idGenerator->generateId());
    pdn->begin();
    
    // Create wireless managers
    LOG_I("SETUP", "Creating QuickdrawWirelessManager...");
    quickdrawWirelessManager = QuickdrawWirelessManager::GetInstance();
    LOG_I("SETUP", "Creating RemoteDebugManager...");
    remoteDebugManager = RemoteDebugManager::GetInstance();
    
    remoteDebugManager->Initialize(WIFI_SSID, WIFI_PASSWORD, BASE_URL);

    quickdrawWirelessManager->initialize(player, 1000);
    
    game = new Quickdraw(player, pdn);
    
    pdn->getDisplay()->
    invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))->
        render();
    delay(3000);
    game->initialize();
}

void loop() {
    pdn->loop();
    game->loop();
}
