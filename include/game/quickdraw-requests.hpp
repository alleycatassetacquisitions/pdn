/*
This file contains http requests for the quickdraw game.

The wireless manager will handle the sending and receiving of these requests.

This file can be thought of as a resources file that contains the predefined http requests for accessing the server.

The server has several api endpoints that we will communicate with.

The first is the player api.
The Player api is a GET request. the path is /api/players/{id}

The player object returned from the server looks like this:
{
    "errors":
    [],
    "data":
    {
        "id": "0025", //The player's id.
        "name": "cyonic", //The player's name.
        "hunter": 1, //1 means player is a bounty hunter, 0 means player is a bounty
        "allegiance": "none", //none, Endline, Helix, or The Resistance
        "faction": "Alleycat" //Freeform text field for the player's faction.
    }
}

the second is the matches api.
The matches api is a PUT request. the path is /matches
It has a string body that is the json of an array of matches.

The expected response looks like this:
{
    "errors":
    [],
    "data":
    "3 Matches added"}
*/

#ifndef QUICKDRAW_REQUESTS_HPP
#define QUICKDRAW_REQUESTS_HPP

#include <string>
#include <ArduinoJson.h>
#include "wireless/wireless-manager.hpp"

// Player API Response Structure
struct PlayerResponse {
    std::string id;
    std::string name;
    bool isHunter;
    int allegiance;
    std::string faction;
    std::vector<std::string> errors;

    // Parse JSON response into this structure
    bool parseFromJson(const std::string& json) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json);
        
        if (error) {
            ESP_LOGE("QuickdrawRequests", "Failed to parse player JSON: %s", error.c_str());
            return false;
        }

        // Check for errors array
        if (doc["errors"].is<JsonArray>()) {
            JsonArray errorsArray = doc["errors"];
            for (JsonVariant errorVar : errorsArray) {
                errors.push_back(errorVar.as<std::string>());
            }
            if (!errors.empty()) {
                ESP_LOGW("QuickdrawRequests", "Player response contains errors");
                return false;
            }
        }

        // Parse data object
        if (!doc["data"].is<JsonObject>()) {
            ESP_LOGE("QuickdrawRequests", "Player response missing data object");
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


// API Request Functions
namespace QuickdrawRequests {

    /**
     * Fetches player information from the server
     * 
     * @param wirelessManager The wireless manager instance to use
     * @param playerId The ID of the player to fetch
     * @param onSuccess Callback function for successful response
     * @param onError Callback function for error handling
     */
    inline void getPlayer(
        WirelessManager* wirelessManager,
        const std::string& playerId,
        std::function<void(const PlayerResponse&)> onSuccess,
        std::function<void(const WirelessErrorInfo&)> onError
    ) {
        std::string path = "/api/players/" + playerId;
        
        wirelessManager->makeHttpRequest(
            path,
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
            onError,
            "GET"
        );
    }

    /**
     * Updates matches on the server
     * 
     * @param wirelessManager The wireless manager instance to use
     * @param matches Vector of matches to update
     * @param onSuccess Callback function for successful response
     * @param onError Callback function for error handling
     */
    inline void updateMatches(
        WirelessManager* wirelessManager,
        std::string matchesJson,
        std::function<void(const std::string&)> onSuccess,
        std::function<void(const WirelessErrorInfo&)> onError
    ) {
        wirelessManager->makeHttpRequest(
            "/api/matches",
            onSuccess,
            onError,
            "PUT",
            matchesJson
        );
    }

} // namespace QuickdrawRequests

#endif // QUICKDRAW_REQUESTS_HPP
