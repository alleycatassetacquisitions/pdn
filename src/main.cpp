#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <FastLED.h>

#include "simple-timer.hpp"
#include "player.hpp"
#include "state-machine.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw.hpp"
#include "id-generator.hpp"
#include "wireless/esp-now-comms.hpp"
#include "wireless/remote-player-manager.hpp"
#include "game/match-manager.hpp"

// Core game objects
Device* pdn = PDN::GetInstance();
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player = new Player();
Quickdraw game = Quickdraw(player, pdn);

// Remote player management
RemotePlayerManager remotePlayers;

void setup() {
    player->setUserID(idGenerator->generateId());
    pdn->begin();
    game.initialize();
    delay(1000);

    ESP_LOGI("PDN", "HW and Game Initialized\n");

    // Initialize WiFi for ESP-NOW use
    WiFi.begin();
    WiFi.enableSTA(true);
    WiFi.channel(6);
    
    remotePlayers.StartBroadcastingPlayerInfo(player, 1000);
    EspNowManager::GetInstance()->SetPacketHandler(PktType::kPlayerInfoBroadcast,
        [](const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg) {
            RemotePlayerManager* manager = (RemotePlayerManager*)userArg;
            manager->ProcessPlayerInfoPkt(srcMacAddr, data, len);
        },
        &remotePlayers);

    ESP_LOGI("PDN", "ESP-NOW and Remote Player Service initialized");
    delay(3000);
}

void loop() {
    pdn->loop();
    EspNowManager::GetInstance()->Update();
    remotePlayers.Update();
    game.loop();
}