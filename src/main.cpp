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
#include "wireless/wireless-manager.hpp"
#include "wireless/remote-player-manager.hpp"
#include "game/match-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"

// Core game objects
Device* pdn = PDN::GetInstance();
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player = new Player();
Quickdraw game = Quickdraw(player, pdn);

// Remote player management
RemotePlayerManager remotePlayers;
QuickdrawWirelessManager *quickdrawWirelessManager = QuickdrawWirelessManager::GetInstance();

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    player->setUserID(idGenerator->generateId());
    pdn->begin();
    game.initialize();
    delay(1000);

    ESP_LOGI("PDN", "HW and Game Initialized\n");

  // Initialize WiFi for ESP-NOW use
  WiFi.begin();
  // STA mode is required for ESP-NOW
  WiFi.enableSTA(true);
  // This could be any Wi-Fi channel, the only requirement is that all devices
  // communicating together on ESP-NOW must use the same channel
  WiFi.channel(6);

  player->setUserID(idGenerator->generateId());
  
  // Initialize the communications manager
  quickdrawWirelessManager->initialize(player, 1000);
  
  remotePlayers.StartBroadcastingPlayerInfo(player, 5000);

  EspNowManager::GetInstance()->SetPacketHandler(PktType::kQuickdrawCommand,
      [](const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg)
        {
          QuickdrawWirelessManager* manager = (QuickdrawWirelessManager*)userArg;
          manager->processQuickdrawCommand(srcMacAddr, data, len);
        },
        quickdrawWirelessManager);


  EspNowManager::GetInstance()->SetPacketHandler(PktType::kPlayerInfoBroadcast,
      [](const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg)
        {
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