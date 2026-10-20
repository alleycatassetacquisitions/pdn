#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * One current assignment from DevicePlayerResponse. Identify by UUID; names may change.
 */
struct DevicePlayerAffiliation {
    std::string categoryId;
    std::string categoryName;
    std::string affiliationId;
    std::string affiliationName;
};

/**
 * Player registration returned by GET /device/players/by-pin/{pin}, resolved in the
 * server's active event.
 */
struct DevicePlayer {
    std::string id;
    std::string eventId;
    std::string pdnCode;
    std::string name;
    int64_t createdAtUnixMs = 0;
    std::string teamId;
    std::string teamName;
    bool active = false;
    std::vector<DevicePlayerAffiliation> affiliations;

    /** @return true when the player is currently assigned to a team. */
    bool hasTeam() const {
        return !teamId.empty();
    }
};
