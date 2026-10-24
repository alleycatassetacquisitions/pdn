#pragma once

#include "utils/uuid-bytes.hpp"
#include <cstddef>
#include <vector>

/** Buffer limits for PlayerV2; the decoder truncates server names to fit. */
namespace PlayerLimits {
constexpr size_t NAME_BUFFER_SIZE = 37;
constexpr size_t MAX_AFFILIATIONS = 3;
}  // namespace PlayerLimits

/** The server team a player is assigned to. A nil id means no team. */
struct Team {
    UUIDBytes id;
    char name[PlayerLimits::NAME_BUFFER_SIZE] = {};
};

/**
 * One current category assignment from the event config. Identify by UUID; names may change.
 */
struct Affiliation {
    UUIDBytes categoryId;
    char categoryName[PlayerLimits::NAME_BUFFER_SIZE] = {};
    UUIDBytes affiliationId;
    char affiliationName[PlayerLimits::NAME_BUFFER_SIZE] = {};
};

/**
 * Server player registration, resolved in the server's active event. Decoded from
 * GET /device/players/by-pin/{pin} by alleycat-server.
 */
struct PlayerV2 {
    UUIDBytes id;
    char name[PlayerLimits::NAME_BUFFER_SIZE] = {};
    Team team;
    std::vector<Affiliation> affiliations;

    /** @return true when the player is currently assigned to a team. */
    bool hasTeam() const { return !team.id.isNil(); }
};
