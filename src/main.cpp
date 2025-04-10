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
WirelessManager* wirelessManager = new WirelessManager(pdn, "network", "password", "http://192.168.1.107:3000");
Quickdraw game = Quickdraw(player, pdn, wirelessManager);

// Remote player management
RemotePlayerManager remotePlayers = RemotePlayerManager();
QuickdrawWirelessManager *quickdrawWirelessManager = QuickdrawWirelessManager::GetInstance();

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    player->setUserID(idGenerator->generateId());
    pdn->begin();
    delay(1000);

    ESP_LOGI("PDN", "HW and Game Initialized\n");

    // Initialize wireless manager
    wirelessManager->initialize();
    
    // Initialize the communications manager
    // quickdrawWirelessManager->initialize(player, 1000);
    
    remotePlayers.StartBroadcastingPlayerInfo(player, 5000);

    // EspNowManager::GetInstance()->SetPacketHandler(PktType::kQuickdrawCommand,
    //     [](const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg)
    //       {
    //         QuickdrawWirelessManager* manager = (QuickdrawWirelessManager*)userArg;
    //         manager->processQuickdrawCommand(srcMacAddr, data, len);
    //       },
    //       quickdrawWirelessManager);

    EspNowManager::GetInstance()->SetPacketHandler(PktType::kPlayerInfoBroadcast,
        [](const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg)
          {
            RemotePlayerManager* manager = (RemotePlayerManager*)userArg;
            manager->ProcessPlayerInfoPkt(srcMacAddr, data, len);
          },
          &remotePlayers);

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
    remotePlayers.Update();
    game.loop();
    wirelessManager->loop();
}