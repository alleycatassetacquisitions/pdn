#pragma once

#include "utils/uuid-bytes.hpp"
#include <cstddef>
#include <vector>

/** Buffer limits for server registration fields on `Player`; the decoder truncates names to fit. */
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
