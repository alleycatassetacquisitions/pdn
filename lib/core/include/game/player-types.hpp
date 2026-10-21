#pragma once

#include "utils/uuid-bytes.hpp"
#include <cstddef>
#include <vector>

/** Buffer limits for app registration profile types; align with DevicePlayerLimits in alleycat-server. */
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
 * App-layer registration profile (core-owned). Populated from decoded @ref DevicePlayer at the
 * alleycat API boundary; excludes wire-only metadata dropped by the protobuf-types model.
 */
struct PlayerV2 {
    UUIDBytes id;
    char name[PlayerLimits::NAME_BUFFER_SIZE] = {};
    Team team;
    std::vector<Affiliation> affiliations;

    /** @return true when the player is currently assigned to a team. */
    bool hasTeam() const { return !team.id.isNil(); }
};
