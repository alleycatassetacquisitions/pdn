#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "game/player.hpp"
#include "game/player-types.hpp"

/** Codec for the PlayerProvisionPacket an FDN sends a cabled PDN. No platform dependencies. */
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
    const size_t count = std::min(affiliations.size(), PlayerLimits::MAX_AFFILIATIONS);
    pkt.affiliationCount = static_cast<uint8_t>(count);
    for (size_t i = 0; i < count; ++i) {
        WireAffiliation& wire = pkt.affiliations[i];
        memcpy(wire.categoryId, affiliations[i].categoryId.bytes, UUIDBytes::SIZE);
        strncpy(wire.categoryName, affiliations[i].categoryName, sizeof(wire.categoryName) - 1);
        memcpy(wire.affiliationId, affiliations[i].affiliationId.bytes, UUIDBytes::SIZE);
        strncpy(wire.affiliationName, affiliations[i].affiliationName,
                sizeof(wire.affiliationName) - 1);
    }
    return pkt;
}

/**
 * Applies a PROFILE_DELIVERED @p pkt to @p outPlayer's registration fields. It
 * comes off the radio, so every name is cut to its buffer and the affiliation
 * count is capped; a rejected packet leaves @p outPlayer untouched.
 *
 * @return false for any other status, a nil player ID, or an empty name.
 */
inline bool decode(const PlayerProvisionPacket& pkt, Player& outPlayer) {
    if (pkt.status != static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED)) return false;
    UUIDBytes playerId{};
    memcpy(playerId.bytes, pkt.playerId, UUIDBytes::SIZE);
    char playerName[PlayerLimits::NAME_BUFFER_SIZE] = {};
    strncpy(playerName, pkt.playerName, PlayerLimits::NAME_BUFFER_SIZE - 1);
    if (playerId.isZero() || playerName[0] == '\0') return false;

    Team team{};
    memcpy(team.id.bytes, pkt.teamId, UUIDBytes::SIZE);
    strncpy(team.name, pkt.teamName, PlayerLimits::NAME_BUFFER_SIZE - 1);

    const size_t count = std::min<size_t>(pkt.affiliationCount, PlayerLimits::MAX_AFFILIATIONS);
    std::vector<Affiliation> affiliations(count);
    for (size_t i = 0; i < count; ++i) {
        const WireAffiliation& wire = pkt.affiliations[i];
        memcpy(affiliations[i].categoryId.bytes, wire.categoryId, UUIDBytes::SIZE);
        strncpy(affiliations[i].categoryName, wire.categoryName,
                PlayerLimits::NAME_BUFFER_SIZE - 1);
        memcpy(affiliations[i].affiliationId.bytes, wire.affiliationId, UUIDBytes::SIZE);
        strncpy(affiliations[i].affiliationName, wire.affiliationName,
                PlayerLimits::NAME_BUFFER_SIZE - 1);
    }
    outPlayer.applyRegistrationProfile(playerId, playerName, team, affiliations);
    return true;
}

}  // namespace PlayerProvisionCodec
