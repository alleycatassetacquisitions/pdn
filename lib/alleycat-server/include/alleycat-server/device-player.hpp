#pragma once

#include "utils/uuid-bytes.hpp"
#include <cstddef>
#include <vector>

/** Device-side limits for DevicePlayer; longer names are truncated and extra affiliations dropped. */
namespace DevicePlayerLimits {
constexpr size_t NAME_BUFFER_SIZE = 37;
constexpr size_t MAX_AFFILIATIONS = 3;
}  // namespace DevicePlayerLimits

/**
 * One current assignment from DevicePlayerResponse. Identify by UUID; names may change.
 */
struct DevicePlayerAffiliation {
    UUIDBytes categoryId;
    char categoryName[DevicePlayerLimits::NAME_BUFFER_SIZE] = {};
    UUIDBytes affiliationId;
    char affiliationName[DevicePlayerLimits::NAME_BUFFER_SIZE] = {};
};

/**
 * Player registration returned by GET /device/players/by-pin/{pin}, resolved in the
 * server's active event.
 */
struct DevicePlayer {
    UUIDBytes id;
    char name[DevicePlayerLimits::NAME_BUFFER_SIZE] = {};
    UUIDBytes teamId;
    char teamName[DevicePlayerLimits::NAME_BUFFER_SIZE] = {};
    std::vector<DevicePlayerAffiliation> affiliations;

    /** @return true when the player is currently assigned to a team. */
    bool hasTeam() const {
        return !teamId.isNil();
    }
};
