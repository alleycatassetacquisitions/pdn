#pragma once

#include "utils/uuid-bytes.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

/** Buffer limits for server registration fields on `Player`; the codec truncates names to fit. */
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

/** PDN-to-FDN over PktType::kPlayerProvisionRequest: look up this PIN for the sender. */
struct PlayerProvisionRequestPacket {
    char pin[5];
} __attribute__((packed));

enum class ProvisionStatus : uint8_t {
    PROFILE_DELIVERED = 0,
    LOOKUP_FAILED = 1,
};

struct WireAffiliation {
    uint8_t categoryId[UUIDBytes::SIZE];
    char categoryName[PlayerLimits::NAME_BUFFER_SIZE];
    uint8_t affiliationId[UUIDBytes::SIZE];
    char affiliationName[PlayerLimits::NAME_BUFFER_SIZE];
} __attribute__((packed));

/**
 * FDN-to-PDN registration over PktType::kPlayerProvision. serverUnixMs carries the
 * FDN's wall clock, the PDN's only source of UTC since it has no HTTP client.
 */
struct PlayerProvisionPacket {
    uint8_t status;
    uint8_t playerId[UUIDBytes::SIZE];
    char playerName[PlayerLimits::NAME_BUFFER_SIZE];
    uint8_t teamId[UUIDBytes::SIZE];
    char teamName[PlayerLimits::NAME_BUFFER_SIZE];
    uint8_t affiliationCount;
    WireAffiliation affiliations[PlayerLimits::MAX_AFFILIATIONS];
    int64_t serverUnixMs;
} __attribute__((packed));
