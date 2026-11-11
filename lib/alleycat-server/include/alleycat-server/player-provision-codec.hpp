#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "game/player.hpp"
#include "game/player-types.hpp"

/**
 * Encodes and decodes the PlayerProvisionPacket an FDN sends a cabled PDN. No
 * platform dependencies; decode applies the profile to the Player it is given.
 */
namespace PlayerProvisionCodec {

/** Packs @p player's registration fields into a PROFILE_DELIVERED packet. */
inline PlayerProvisionPacket encode(const Player& player) {
    PlayerProvisionPacket pkt{};
    pkt.status = static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED);
    memcpy(pkt.playerId, player.getServerPlayerId().bytes, UUIDBytes::SIZE);
    strncpy(pkt.playerName, player.getServerPlayerName(), sizeof(pkt.playerName) - 1);
    const Team& team = player.getRegistrationTeam();
    memcpy(pkt.teamId, team.id.bytes, UUIDBytes::SIZE);
    strncpy(pkt.teamName, team.name, sizeof(pkt.teamName) - 1);

    const std::vector<Affiliation>& affiliations = player.getRegistrationAffiliations();
    const uint8_t count =
    static_cast<uint8_t>(std::min<size_t>(affiliations.size(), PlayerLimits::MAX_AFFILIATIONS));
    pkt.affiliationCount = count;
    for (uint8_t i = 0; i < count; ++i) {
        memcpy(pkt.affiliations[i].categoryId, affiliations[i].categoryId.bytes, UUIDBytes::SIZE);
        strncpy(pkt.affiliations[i].categoryName, affiliations[i].categoryName,
                sizeof(pkt.affiliations[i].categoryName) - 1);
        memcpy(pkt.affiliations[i].affiliationId, affiliations[i].affiliationId.bytes,
               UUIDBytes::SIZE);
        strncpy(pkt.affiliations[i].affiliationName, affiliations[i].affiliationName,
                sizeof(pkt.affiliations[i].affiliationName) - 1);
    }
    return pkt;
}

/**
 * Applies a PROFILE_DELIVERED @p pkt to @p outPlayer's registration fields. Validates
 * first, so a rejected packet leaves @p outPlayer untouched.
 *
 * @return false for any other status, a nil player ID, or an empty name.
 */
inline bool decode(const PlayerProvisionPacket& pkt, Player& outPlayer) {
    if (pkt.status != static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED)) {
        return false;
    }
    UUIDBytes playerId{};
    memcpy(playerId.bytes, pkt.playerId, UUIDBytes::SIZE);
    if (playerId.isZero()) {
        return false;
    }
    char playerName[PlayerLimits::NAME_BUFFER_SIZE] = {};
    strncpy(playerName, pkt.playerName, PlayerLimits::NAME_BUFFER_SIZE - 1);
    if (playerName[0] == '\0') {
        return false;
    }

    Team team{};
    memcpy(team.id.bytes, pkt.teamId, UUIDBytes::SIZE);
    strncpy(team.name, pkt.teamName, PlayerLimits::NAME_BUFFER_SIZE - 1);

    const size_t count = std::min<size_t>(pkt.affiliationCount, PlayerLimits::MAX_AFFILIATIONS);
    std::vector<Affiliation> affiliations;
    affiliations.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Affiliation aff{};
        memcpy(aff.categoryId.bytes, pkt.affiliations[i].categoryId, UUIDBytes::SIZE);
        strncpy(aff.categoryName, pkt.affiliations[i].categoryName,
                PlayerLimits::NAME_BUFFER_SIZE - 1);
        memcpy(aff.affiliationId.bytes, pkt.affiliations[i].affiliationId, UUIDBytes::SIZE);
        strncpy(aff.affiliationName, pkt.affiliations[i].affiliationName,
                PlayerLimits::NAME_BUFFER_SIZE - 1);
        affiliations.push_back(aff);
    }

    outPlayer.applyRegistrationProfile(playerId, playerName, team, affiliations);
    return true;
}

}  // namespace PlayerProvisionCodec
