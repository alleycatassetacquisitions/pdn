#pragma once

#include "utils/uuid-bytes.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

/** Buffer limits for server registration fields on `Player`; the codec truncates names to fit. */
namespace PlayerLimits {
constexpr size_t NAME_BUFFER_SIZE = 32;
constexpr size_t MAX_AFFILIATIONS = 5;
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
 * A player's totals over the matches the server scores in the active event; all
 * zero when none were sent or accepted. Display only. Each field spans the full
 * 32 bits off the wire, so sums and products of them need a wider type.
 */
struct PlayerTotals {
    uint32_t matchesPlayed = 0;
    uint32_t wins = 0;
    uint32_t averageDrawTimeMs = 0;

    /** @return false when the totals claim more wins than matches played. */
    bool isConsistent() const { return wins <= matchesPlayed; }
};

enum class ProvisionStatus : uint8_t {
    PROFILE_DELIVERED = 0,
    LOOKUP_FAILED = 1,
};

// How long a band lookup may wait for Wi-Fi and the server before the FDN gives up.
constexpr unsigned long PROVISION_LOOKUP_TIMEOUT_MS = 8000;

struct WireAffiliation {
    uint8_t categoryId[UUIDBytes::SIZE];
    char categoryName[PlayerLimits::NAME_BUFFER_SIZE];
    uint8_t affiliationId[UUIDBytes::SIZE];
    char affiliationName[PlayerLimits::NAME_BUFFER_SIZE];
} __attribute__((packed));

/**
 * FDN-to-PDN registration over PktType::kPlayerProvision, sent to the PDN on the
 * jack whose registration the tap answered. The three totals are the player's
 * PlayerTotals, all zero when the lookup yielded none.
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
    uint32_t totalMatchesPlayed;
    uint32_t totalWins;
    uint32_t totalAverageDrawTimeMs;
} __attribute__((packed));
static_assert(sizeof(PlayerProvisionPacket) == 591, "PlayerProvisionPacket wire size");
