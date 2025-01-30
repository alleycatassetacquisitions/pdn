#include <Arduino.h>
#include <WiFi.h>

#include "player.hpp"
#include "state-machine.hpp"
#include "device/pdn.hpp"
#include "game/quickdraw.hpp"
#include "id-generator.hpp"
#include "wireless/esp-now-comms.hpp"
#include "wireless/remote-player-manager.hpp"

//GAME & ROLE
Device* pdn = PDN::GetInstance();
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player = new Player();


//WIRELESS
RemotePlayerManager* remotePlayers = RemotePlayerManager::GetInstance();

Quickdraw game = Quickdraw(player, pdn, remotePlayers);

// String stripWhitespace(String input) {
//   String output;
//   for (int i = 0; i < input.length(); i++) {
//     char c = input.charAt(i);
//     // Check if the character is not a whitespace
//     if (!isspace(c) || c > 127) {
//       output += c; // Append non-whitespace character to the output string
//     }
//   }
//   return output;
// }

// String dumpMatchesToJson() {
//   StaticJsonDocument<512> doc;
//   JsonArray matchesArray = doc.to<JsonArray>();
//   StaticJsonDocument<128> match;
//   JsonObject matchObj;
//   for (int i = 0; i < numMatches; i++) {
//     matchObj = match.to<JsonObject>();
//     matches[i].fillJsonObject(matchObj);
//     matchesArray.add(matchObj);
//   }
//   String output;
//   serializeJson(matchesArray, output);
//   return output;
// }

void setup(void) {
  //esp_log_level_set("*", ESP_LOG_VERBOSE);
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
  
  remotePlayers->initialize(player, 1000);
  EspNowManager::GetInstance()->SetPacketHandler(PktType::kPlayerInfoBroadcast,
      [](const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg)
        {
          RemotePlayerManager::GetInstance()->ProcessPlayerInfoPkt(srcMacAddr, data, len);
        },
        &remotePlayers);

  ESP_LOGI("PDN", "ESP-NOW and Remote Player Service initialized");

  pdn->setButtonClick(ButtonInteraction::MULTI_CLICK, ButtonIdentifier::SECONDARY_BUTTON, [] {
    ESP_LOGI("PDN", "Secondary Multiclick fired, Button Clicked Count: %i",
      pdn->buttonMultiClickCount(ButtonIdentifier::SECONDARY_BUTTON));

    if(pdn->buttonMultiClickCount(ButtonIdentifier::SECONDARY_BUTTON) > 3) {
      player->setHunter(!player->isHunter());
      game.reset();
    }
  });

  delay(3000);

}

void loop(void) {
  pdn->loop();
  EspNowManager::GetInstance()->Update();

  game.loop();
}

String mac2String(byte ar[]) {
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%02X", ar[i]); // J-M-L: slight modification, added the 0 in the format for padding 
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}