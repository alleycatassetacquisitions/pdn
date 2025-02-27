#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <FastLED.h>
#include <string>

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

// WiFi credentials - replace with your actual WiFi credentials
const char* WIFI_SSID = "googletest";
const char* WIFI_PASSWORD = "test1234";

// Core game objects
Device* pdn = PDN::GetInstance();
IdGenerator* idGenerator = IdGenerator::GetInstance();
Player* player = new Player();
Quickdraw game = Quickdraw(player, pdn);

// Remote player management
// RemotePlayerManager remotePlayers;
// Wireless manager for HTTP requests
WirelessManager* wirelessManager = nullptr;
// Match manager for creating and tracking matches
MatchManager* matchManager = nullptr;

// Test function to create a match and send it via HTTP PUT
void testMatchesAndPutRequest() {
    ESP_LOGI("PDN", "Starting match test with HTTP PUT request");
    
    // Create a test match with hunter and bounty IDs
    std::string hunterId = player->getUserID();
    std::string bountyId = "opponent-" + std::to_string(random(1000, 9999));
    
    ESP_LOGI("PDN", "Creating match with hunter ID: %s, bounty ID: %s", 
             hunterId.c_str(), bountyId.c_str());
    
    // Create the match
    Match* match = matchManager->createMatch(hunterId, bountyId);
    
    if (!match) {
        ESP_LOGE("PDN", "Failed to create match");
        return;
    }
    
    // Get the match ID
    std::string matchId = match->getMatchId();
    ESP_LOGI("PDN", "Match created with ID: %s", matchId.c_str());
    
    // Update match with test data
    bool success = matchManager->updateMatch(
        matchId,
        true,  // hunter wins
        random(500, 2000),  // hunter draw time between 500-2000ms
        random(1000, 3000)  // bounty draw time between 1000-3000ms
    );
    
    if (!success) {
        ESP_LOGE("PDN", "Failed to update match");
        return;
    }
    
    // Finalize the match
    success = matchManager->finalizeMatch(matchId);
    
    if (!success) {
        ESP_LOGE("PDN", "Failed to finalize match");
        return;
    }
    
    ESP_LOGI("PDN", "Match created and finalized successfully");
    
    // Get all matches as JSON with "matches" wrapper
    std::string wrappedJson = matchManager->toJson();
    ESP_LOGI("PDN", "Wrapped JSON payload: %s", wrappedJson.c_str());
    
    // Make PUT request with the match data
    if (wirelessManager) {
        // Test URL - using HTTPS with certificate verification disabled in the wireless manager
        std::string url = "https://alleycat-server-bun.onrender.com/api/matches";
        
        ESP_LOGI("PDN", "Making PUT request to %s with match data", url.c_str());
        
        // Define success callback
        auto onSuccess = [](const std::string& response) {
            ESP_LOGI("PDN", "PUT request successful!");
            ESP_LOGI("PDN", "Response: %s", response.c_str());
        };
        
        // Define error callback
        auto onError = [](const WirelessErrorInfo& error) {
            ESP_LOGE("PDN", "PUT request failed! Error: %s", error.message.c_str());
        };
        
        // Make the request
        wirelessManager->makeHttpRequest(
            url,
            onSuccess,
            onError,
            "PUT",  // Using PUT method now that the wireless manager supports it
            wrappedJson  // Using the wrapped JSON payload
        );
        
        ESP_LOGI("PDN", "PUT request queued");
    } else {
        ESP_LOGE("PDN", "Wireless manager not initialized, cannot make PUT request");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);

    // Set player ID
    player->setUserID(idGenerator->generateId());
    
    // Initialize hardware
    pdn->begin();
    game.initialize();
    delay(1000);

    ESP_LOGI("PDN", "HW and Game Initialized\n");

    // Initialize WiFi for ESP-NOW use
    // WiFi.begin();
    // WiFi.enableSTA(true);
    // WiFi.channel(6);

    // Initialize ESP-NOW communication
    // remotePlayers.StartBroadcastingPlayerInfo(player, 1000);
    // EspNowManager::GetInstance()->SetPacketHandler(PktType::kPlayerInfoBroadcast,
    //       [](const uint8_t* srcMacAddr, const uint8_t* data, const size_t len, void* userArg) {
    //         RemotePlayerManager* manager = (RemotePlayerManager*)userArg;
    //         manager->ProcessPlayerInfoPkt(srcMacAddr, data, len);
    //       },
    //       &remotePlayers);

    ESP_LOGI("PDN", "ESP-NOW and Remote Player Service initialized");
    
    // Initialize MatchManager
    matchManager = MatchManager::GetInstance();
    ESP_LOGI("PDN", "Match Manager initialized");
    
    // Initialize WirelessManager for HTTP requests
    wirelessManager = new WirelessManager(pdn, WIFI_SSID, WIFI_PASSWORD);
    wirelessManager->initialize();
    ESP_LOGI("PDN", "Wireless Manager initialized");
    
    // Wait for everything to initialize
    delay(3000);
    
    // Run the match test with HTTP PUT request
    ESP_LOGI("PDN", "Starting match test...");
    testMatchesAndPutRequest();
}

void loop() {
    // Core system updates
    pdn->loop();
    // EspNowManager::GetInstance()->Update();
    // remotePlayers.Update();
    game.loop();
    
    // Process wireless operations (HTTP requests, etc.)
    if (wirelessManager) {
        wirelessManager->loop();
    }
}