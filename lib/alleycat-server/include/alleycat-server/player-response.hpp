#pragma once

#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "device/drivers/logger.hpp"

/**
 * JSON body for GET /api/players/{id} (Alleycat REST — not protobuf).
 * Shape: { "data": { "id", "name", "hunter", "allegiance", "faction" }, "errors": [...] }
 */
struct PlayerResponse {
    std::string id;
    std::string name;
    bool isHunter;
    int allegiance;
    std::string faction;
    std::vector<std::string> errors;

    bool parseFromJson(const std::string& json) {
        errors.clear();

        JsonDocument doc;
        DeserializationError parseError = deserializeJson(doc, json);
        if (parseError) {
            LOG_E("PlayerApi", "Failed to parse player JSON: %s", parseError.c_str());
            return false;
        }

        if (doc["errors"].is<JsonArray>()) {
            JsonArray errorsArray = doc["errors"];
            for (JsonVariant errorVar : errorsArray) {
                errors.push_back(errorVar.as<std::string>());
            }
            if (!errors.empty()) {
                LOG_W("PlayerApi", "Player response contains errors");
                return false;
            }
        }

        if (!doc["data"].is<JsonObject>()) {
            LOG_E("PlayerApi", "Player response missing data object");
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
