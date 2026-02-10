#pragma once

#include <string>
#include <ArduinoJson.h>
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

// Player API Response Structure
struct PlayerResponse {
    std::string id;
    std::string name;
    bool isHunter;
    int allegiance;
    std::string faction;
    std::vector<std::string> errors;

    // Konami progress fields — default to zero/empty for backward compatibility
    uint8_t konamiProgress = 0;
    uint8_t equippedColorProfile = 0;
    std::vector<uint8_t> colorProfileEligibility;

    bool parseFromJson(const std::string& json) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json);

        if (error) {
            LOG_E("QuickdrawRequests", "Failed to parse player JSON: %s", error.c_str());
            return false;
        }

        if (doc["errors"].is<JsonArray>()) {
            JsonArray errorsArray = doc["errors"];
            for (JsonVariant errorVar : errorsArray) {
                errors.push_back(errorVar.as<std::string>());
            }
            if (!errors.empty()) {
                LOG_W("QuickdrawRequests", "Player response contains errors");
                return false;
            }
        }

        if (!doc["data"].is<JsonObject>()) {
            LOG_E("QuickdrawRequests", "Player response missing data object");
            return false;
        }

        JsonObject data = doc["data"];
        id = data["id"].as<std::string>();
        name = data["name"].as<std::string>();
        isHunter = data["hunter"].as<bool>();
        allegiance = data["allegiance"].as<int>();
        faction = data["faction"].as<std::string>();

        // Optional fields — default to 0/empty if missing (backward compat)
        if (data.containsKey("konami_progress")) {
            konamiProgress = data["konami_progress"].as<uint8_t>();
        }
        if (data.containsKey("equipped_color_profile")) {
            equippedColorProfile = data["equipped_color_profile"].as<uint8_t>();
        }
        if (data.containsKey("color_profile_eligibility")) {
            for (JsonVariant v : data["color_profile_eligibility"].as<JsonArray>()) {
                colorProfileEligibility.push_back(v.as<uint8_t>());
            }
        }

        return true;
    }
};

namespace QuickdrawRequests {

    /**
     * Fetch player data from the server.
     * Automatically switches to WiFi mode if needed.
     */
    inline void getPlayer(
        WirelessManager* wirelessManager,
        const std::string& playerId,
        const std::function<void(const PlayerResponse&)>& onSuccess,
        const std::function<void(const WirelessErrorInfo&)>& onError
    ) {
        std::string path = "/api/players/" + playerId;
        
        HttpRequest request(
            path,
            "GET",
            "",
            [onSuccess, onError](const std::string& response) {
                PlayerResponse playerResponse;
                if (playerResponse.parseFromJson(response)) {
                    onSuccess(playerResponse);
                } else {
                    onError({
                        WirelessError::INVALID_RESPONSE,
                        "Failed to parse player response",
                        false
                    });
                }
            },
            onError
        );
        
        wirelessManager->queueHttpRequest(request);
    }

    /**
     * Upload match results to the server.
     * Automatically switches to WiFi mode if needed.
     */
    inline void updateMatches(
        WirelessManager* wirelessManager,
        const std::string& matchesJson,
        const std::function<void(const std::string&)>& onSuccess,
        const std::function<void(const WirelessErrorInfo&)>& onError
    ) {
        HttpRequest request(
            "/api/matches",
            "PUT",
            matchesJson,
            onSuccess,
            onError
        );
        
        wirelessManager->queueHttpRequest(request);
    }

    /**
     * Upload player progress (Konami buttons, game results) to the server.
     * Automatically switches to WiFi mode if needed.
     */
    inline void updateProgress(
        WirelessManager* wirelessManager,
        const std::string& playerId,
        const std::string& progressJson,
        const std::function<void(const std::string&)>& onSuccess,
        const std::function<void(const WirelessErrorInfo&)>& onError
    ) {
        std::string path = "/api/players/" + playerId + "/progress";

        HttpRequest request(
            path,
            "PUT",
            progressJson,
            onSuccess,
            onError
        );

        wirelessManager->queueHttpRequest(request);
    }

}
