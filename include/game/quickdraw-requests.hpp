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
     * Upload FDN results to the server.
     * Automatically switches to WiFi mode if needed.
     */
    inline void uploadFdnResults(
        WirelessManager* wirelessManager,
        const std::string& resultsJson,
        const std::function<void(const std::string&)>& onSuccess,
        const std::function<void(const WirelessErrorInfo&)>& onError
    ) {
        HttpRequest request(
            "/api/fdn-results",
            "POST",
            resultsJson,
            onSuccess,
            onError
        );

        wirelessManager->queueHttpRequest(request);
    }

    /**
     * Fetch progress data from the server.
     * Automatically switches to WiFi mode if needed.
     */
    inline void getProgress(
        WirelessManager* wirelessManager,
        const std::function<void(const std::string&)>& onSuccess,
        const std::function<void(const WirelessErrorInfo&)>& onError
    ) {
        HttpRequest request(
            "/api/progress",
            "GET",
            "",
            onSuccess,
            onError
        );

        wirelessManager->queueHttpRequest(request);
    }

}
