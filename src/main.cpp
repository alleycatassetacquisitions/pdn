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
WifiConfig wifiConfig(WIFI_SSID, WIFI_PASSWORD, BASE_URL);


// ESP32-s3 Drivers
Esp32S3Clock clockDriver(PLATFORM_CLOCK_DRIVER_NAME);
SSD1306U8G2Driver displayDriver(DISPLAY_DRIVER_NAME);
Esp32S31ButtonDriver primaryButtonDriver(PRIMARY_BUTTON_DRIVER_NAME, primaryButtonPin);
Esp32S31ButtonDriver secondaryButtonDriver(SECONDARY_BUTTON_DRIVER_NAME, secondaryButtonPin);
WS2812BFastLEDDriver lightDriver(LIGHT_DRIVER_NAME);
Esp32S3HapticsDriver hapticsDriver(HAPTICS_DRIVER_NAME, motorPin);
Esp32s3SerialOut serialOutDriver(SERIAL_OUT_DRIVER_NAME);
Esp32s3SerialIn serialInDriver(SERIAL_IN_DRIVER_NAME);
Esp32S3HttpClient httpClientDriver(HTTP_CLIENT_DRIVER_NAME, &wifiConfig);
EspNowManager* peerCommsDriver = EspNowManager::CreateEspNowManager(PEER_COMMS_DRIVER_NAME);
Esp32S3Logger loggerDriver(LOGGER_DRIVER_NAME);
Esp32S3PrefsDriver storageDriver(STORAGE_DRIVER_NAME, PREF_NAMESPACE);

// Driver Configuration
DriverConfig pdnConfig = {
    {DISPLAY_DRIVER_NAME, &displayDriver},
    {PRIMARY_BUTTON_DRIVER_NAME, &primaryButtonDriver},
    {SECONDARY_BUTTON_DRIVER_NAME, &secondaryButtonDriver},
    {LIGHT_DRIVER_NAME, &lightDriver},
    {HAPTICS_DRIVER_NAME, &hapticsDriver},
    {SERIAL_OUT_DRIVER_NAME, &serialOutDriver},
    {SERIAL_IN_DRIVER_NAME, &serialInDriver},
    {HTTP_CLIENT_DRIVER_NAME, &httpClientDriver},
    {PEER_COMMS_DRIVER_NAME, peerCommsDriver},
    {PLATFORM_CLOCK_DRIVER_NAME, &clockDriver},
    {LOGGER_DRIVER_NAME, &loggerDriver},
};

// Core game objects
Device* pdn = PDN::createPDN(pdnConfig);
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player = new Player();

// Game instance
Quickdraw* game = nullptr;

// Remote player management
QuickdrawWirelessManager* quickdrawWirelessManager = QuickdrawWirelessManager::GetInstance();
RemoteDebugManager* remoteDebugManager = RemoteDebugManager::GetInstance();

void setupEspNow() {
    // Set up WiFi for ESP-NOW - ESP-NOW requires STA mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    WiFi.channel(6);
    
    // Initialize ESP-NOW
    esp_err_t err = EspNowManager::GetInstance()->initialize();
    if (err == ESP_OK) {
        LOG_I("PDN", "ESP-NOW initialized successfully");
        
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
    } else {
        LOG_E("PDN", "ESP-NOW initialization failed: %d", err);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    // Initialize platform abstractions
    g_logger = &loggerDriver;
    SimpleTimer::setPlatformClock(&clockDriver);

    player->setUserID(idGenerator->generateId());
    pdn->begin();
    delay(1000);

    LOG_I("PDN", "HW and Game Initialized");

    // Initialize ESP-NOW for peer-to-peer communication
    setupEspNow();
    
    // Initialize remote debug manager
    remoteDebugManager->Initialize(WIFI_SSID, WIFI_PASSWORD, BASE_URL);

    // Initialize quickdraw wireless manager
    quickdrawWirelessManager->initialize(player, 1000);

    LOG_I("PDN", "Wireless services initialized");
    
    // Create game with HTTP client
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
