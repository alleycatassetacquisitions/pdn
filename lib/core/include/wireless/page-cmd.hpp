#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <ArduinoJson.h>

#include "game/player.hpp"

/** Parsed DNN PageCmd JSON (mc/dnn/.../page). Field names match dnn_commands.proto. */
struct PageCmd {
    std::string id;
    std::string text;
    int durationSeconds = 10;
    bool scroll = false;
    std::string playerId;
    std::string role;
    std::string neocorp;
};

inline bool parsePageCmdJson(const uint8_t* data, size_t length, PageCmd& out) {
    if (data == nullptr || length == 0) {
        return false;
    }

    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, reinterpret_cast<const char*>(data), length);
    if (err) {
        return false;
    }

    out.id = doc["id"] | "";
    out.text = doc["text"] | "";
    out.durationSeconds = doc["duration"] | 10;
    out.scroll = doc["scroll"] | false;
    out.playerId = doc["player_id"] | "";
    out.role = doc["role"] | "";
    out.neocorp = doc["neocorp"] | "";

    if (out.durationSeconds < 0) {
        out.durationSeconds = 0;
    }
    return !out.text.empty() || !out.id.empty();
}

/**
 * Empty stamps accept everyone. Non-empty stamps must match the local player.
 * player_id matches legacy user id or formatted server UUID. role is hunter|bounty.
 * neocorp maps to Allegiance (helix/endline/reboot|resistance/freelancer|alleycat).
 */
inline bool pageCmdMatchesPlayer(const PageCmd& cmd, const Player* player) {
    if (player == nullptr) {
        return true;
    }

    if (!cmd.playerId.empty()) {
        bool idMatch = cmd.playerId == player->getUserID();
        if (!idMatch && player->hasRegistrationProfile() && !player->getServerPlayerId().isZero()) {
            char serverId[UUIDBytes::TEXT_BUFFER_SIZE] = {};
            player->getServerPlayerId().format(serverId);
            idMatch = cmd.playerId == serverId;
        }
        if (!idMatch) {
            return false;
        }
    }

    if (!cmd.role.empty()) {
        const bool hunter = player->isHunter();
        if (cmd.role == "hunter") {
            if (!hunter) {
                return false;
            }
        } else if (cmd.role == "bounty") {
            if (hunter) {
                return false;
            }
        } else if (cmd.role == "unassigned") {
            // Accept any logged-in player when role is explicitly unassigned.
        } else {
            return false;
        }
    }

    if (!cmd.neocorp.empty()) {
        Allegiance want = Allegiance::ALLEYCAT;
        if (cmd.neocorp == "helix") {
            want = Allegiance::HELIX;
        } else if (cmd.neocorp == "endline") {
            want = Allegiance::ENDLINE;
        } else if (cmd.neocorp == "reboot" || cmd.neocorp == "resistance") {
            want = Allegiance::RESISTANCE;
        } else if (cmd.neocorp == "freelancer" || cmd.neocorp == "alleycat") {
            want = Allegiance::ALLEYCAT;
        } else {
            return false;
        }
        if (player->getAllegiance() != want) {
            return false;
        }
    }

    return true;
}
