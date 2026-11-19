#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "game/player.hpp"
#include "game/player-types.hpp"

/** Encodes the PlayerProvisionPacket an FDN sends a cabled PDN. No platform dependencies. */
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

}  // namespace PlayerProvisionCodec
