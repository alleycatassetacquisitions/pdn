#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <FastLED.h>
#include <Preferences.h>

#include "simple-timer.hpp"
#include "player.hpp"
#include "state-machine.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw.hpp"
#include "id-generator.hpp"
#include "wireless/esp-now-comms.hpp"
#include "wireless/wireless-manager.hpp"
#include "wireless/remote-player-manager.hpp"
#include "game/match-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"

// Core game objects
Device* pdn = PDN::GetInstance();
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player = new Player();

const char* WIFI_SSID = "NeoCore Networks";
const char* WIFI_PASSWORD = "AlleyCatBountyHunting1";
const char* BASE_URL = "http://alleycat-server.local:3000";

WirelessManager* wirelessManager = new WirelessManager(pdn, WIFI_SSID, WIFI_PASSWORD, BASE_URL);
Quickdraw game = Quickdraw(player, pdn, wirelessManager);

// Remote player management
// RemotePlayerManager remotePlayers;
QuickdrawWirelessManager *quickdrawWirelessManager = QuickdrawWirelessManager::GetInstance();
RemoteDebugManager* remoteDebugManager = RemoteDebugManager::GetInstance();

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    player->setUserID(idGenerator->generateId());
    pdn->begin();
    delay(1000);

    ESP_LOGI("PDN", "HW and Game Initialized\n");

    // Initialize wireless manager
    wirelessManager->initialize();
    
    // Set up WiFi for ESP-NOW use - ESP-NOW requires STA mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);  // Don't connect to any AP
    WiFi.channel(6);  // Set a consistent channel for ESP-NOW communication
    
    ESP_LOGI("PDN", "WiFi configured for ESP-NOW");

    remoteDebugManager->Initialize(WIFI_SSID, WIFI_PASSWORD, BASE_URL);

    // Initialize the communications manager
    quickdrawWirelessManager->initialize(player, 1000);

    ESP_LOGI("PDN", "ESP-NOW and Remote Player Service initialized");
    pdn->
    invalidateScreen()->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::LOGO_LEFT))->
        drawImage(Quickdraw::getImageForAllegiance(Allegiance::ALLEYCAT, ImageType::STAMP))->
        render();
    delay(3000);
    game.initialize();
}

void loop() {
    pdn->loop();
    // EspNowManager::GetInstance()->Update();
    // remotePlayers.Update();
    game.loop();
    wirelessManager->loop();
}