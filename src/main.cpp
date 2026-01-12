#include <Arduino.h>
#include <WiFi.h>
#include <FastLED.h>
#include <Preferences.h>

#include "logger.hpp"
#include "utils/esp32-s3-logger.hpp"
#include "utils/simple-timer.hpp"
#include "utils/esp32-s3-clock.hpp"
#include "player.hpp"
#include "state-machine.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw.hpp"
#include "id-generator.hpp"
#include "wireless/esp-now-comms.hpp"
#include "wireless/esp32-s3-http-client.hpp"
#include "wireless/remote-player-manager.hpp"
#include "game/match-manager.hpp"
#include "wireless-types.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "peer-comms-interface.hpp"

// Core game objects
Device* pdn = PDN::GetInstance();
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player = new Player();

// WiFi configuration
const char* WIFI_SSID = "NeoCore Networks";
const char* WIFI_PASSWORD = "AlleyCatBountyHunting1";
const char* BASE_URL = "http://alleycat-server.local:3000";
WifiConfig wifiConfig(WIFI_SSID, WIFI_PASSWORD, BASE_URL);

// HTTP client for server communication
Esp32S3HttpClient httpClient;

// Game instance
Quickdraw* game = nullptr;

// Remote player management
QuickdrawWirelessManager* quickdrawWirelessManager = QuickdrawWirelessManager::GetInstance();
RemoteDebugManager* remoteDebugManager = RemoteDebugManager::GetInstance();

// Logger
Esp32S3Logger logger;

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
    g_logger = &logger;
    SimpleTimer::setPlatformClock(new Esp32S3Clock());

    player->setUserID(idGenerator->generateId());
    pdn->begin();
    delay(1000);

    LOG_I("PDN", "HW and Game Initialized");

    // Initialize ESP-NOW for peer-to-peer communication
    setupEspNow();
    
    // Initialize HTTP client for server communication
    httpClient.initialize(&wifiConfig);
    
    // Initialize remote debug manager
    remoteDebugManager->Initialize(WIFI_SSID, WIFI_PASSWORD, BASE_URL);

    // Initialize quickdraw wireless manager
    quickdrawWirelessManager->initialize(player, 1000);

    LOG_I("PDN", "Wireless services initialized");
    
    // Create game with HTTP client
    game = new Quickdraw(player, pdn, &httpClient);
    
    pdn->
    invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))->
        render();
    delay(3000);
    game->initialize();
}

void loop() {
    pdn->loop();
    EspNowManager::GetInstance()->update();
    httpClient.update();
    game->loop();
}
