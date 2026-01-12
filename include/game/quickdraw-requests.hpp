#pragma once

#include <string>
#include <ArduinoJson.h>
#include "logger.hpp"
#include "http-client-interface.hpp"
#include "wireless-types.hpp"

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

    inline void getPlayer(
        HttpClientInterface* httpClient,
        const std::string& playerId,
        std::function<void(const PlayerResponse&)> onSuccess,
        std::function<void(const WirelessErrorInfo&)> onError
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
        
        httpClient->queueRequest(request);
    }

    inline void updateMatches(
        HttpClientInterface* httpClient,
        std::string matchesJson,
        std::function<void(const std::string&)> onSuccess,
        std::function<void(const WirelessErrorInfo&)> onError
    ) {
        HttpRequest request(
            "/api/matches",
            "PUT",
            matchesJson,
            onSuccess,
            onError
        );
        
        httpClient->queueRequest(request);
    }

}
