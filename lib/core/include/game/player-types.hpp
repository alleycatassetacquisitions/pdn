#pragma once

#include "utils/uuid-bytes.hpp"
#include <cstddef>
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

/** What a PlayerProvisionPacket carries. */
enum class ProvisionStatus : uint8_t {
    PROFILE_DELIVERED = 0,
    LOOKUP_FAILED = 1,
};

/** One affiliation on the wire, names NUL-terminated. */
struct WireAffiliation {
    uint8_t categoryId[UUIDBytes::SIZE];
    char categoryName[PlayerLimits::NAME_BUFFER_SIZE];
    uint8_t affiliationId[UUIDBytes::SIZE];
    char affiliationName[PlayerLimits::NAME_BUFFER_SIZE];
} __attribute__((packed));

/**
 * FDN to PDN over PktType::kPlayerProvision, sent to the MAC of the PDN whose
 * registration a tap answered, after its record key went over that PDN's jack.
 * Only `status` means anything when it is LOOKUP_FAILED.
 */
struct PlayerProvisionPacket {
    uint8_t seqId;
    uint8_t status;
    uint8_t playerId[UUIDBytes::SIZE];
    char playerName[PlayerLimits::NAME_BUFFER_SIZE];
    uint8_t teamId[UUIDBytes::SIZE];
    char teamName[PlayerLimits::NAME_BUFFER_SIZE];
    uint8_t affiliationCount;
    WireAffiliation affiliations[PlayerLimits::MAX_AFFILIATIONS];
} __attribute__((packed));
