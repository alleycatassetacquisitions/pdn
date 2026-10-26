#pragma once

#include <gtest/gtest.h>

#include "game/player-provider.hpp"
#include "game/player-types.hpp"
#include "id-generator.hpp"
#include "uuid-bytes-tests.hpp"

inline void applyTestRegistration(Player* player, const char* playerId, const char* name) {
    UUIDBytes serverId = makeTestUuid(playerId);
    Team team;
    team.id = makeTestUuid("5a4b3c2d-1e0f-4a9b-8c7d-6e5f4a3b2c1d");
    IdGenerator::copyId(team.name, "Hunters");
    std::vector<Affiliation> affiliations(1);
    affiliations[0].categoryId = makeTestUuid("c0ffee00-0000-4000-8000-000000000001");
    IdGenerator::copyId(affiliations[0].categoryName, "Allegiance");
    affiliations[0].affiliationId = makeTestUuid("c0ffee00-0000-4000-8000-000000000002");
    IdGenerator::copyId(affiliations[0].affiliationName, "Helix");
    player->applyRegistrationProfile(serverId, name, team, affiliations);
}

inline void playerProviderStartsEmpty() {
    PlayerProvider playerProvider;
    EXPECT_FALSE(playerProvider.hasPlayer());
}

inline void playerProviderReturnsEmptyRegistrationFields() {
    PlayerProvider playerProvider;
    const Player* player = playerProvider.getPlayer();
    EXPECT_TRUE(player->getServerPlayerId().isZero());
    EXPECT_STREQ(player->getServerPlayerName(), "");
    EXPECT_FALSE(player->hasTeam());
    EXPECT_TRUE(player->getRegistrationAffiliations().empty());
}

inline void playerProviderStoresProfile() {
    PlayerProvider playerProvider;
    applyTestRegistration(playerProvider.getPlayer(), "11111111-2222-4333-8444-555555555555", "Bo");

    ASSERT_TRUE(playerProvider.hasPlayer());
    const Player* player = playerProvider.getPlayer();
    EXPECT_EQ(player->getServerPlayerId(), makeTestUuid("11111111-2222-4333-8444-555555555555"));
    EXPECT_STREQ(player->getServerPlayerName(), "Bo");
    EXPECT_TRUE(player->hasTeam());
    EXPECT_STREQ(player->getRegistrationTeam().name, "Hunters");
    ASSERT_EQ(player->getRegistrationAffiliations().size(), 1u);
    EXPECT_STREQ(player->getRegistrationAffiliations()[0].affiliationName, "Helix");
}

inline void playerProviderOverwriteReplacesProfile() {
    PlayerProvider playerProvider;
    applyTestRegistration(playerProvider.getPlayer(), "11111111-2222-4333-8444-555555555555", "Bo");

    UUIDBytes replacementId = makeTestUuid("66666666-7777-4888-8999-aaaaaaaaaaaa");
    Team emptyTeam;
    playerProvider.getPlayer()->applyRegistrationProfile(replacementId, "Cy", emptyTeam, {});

    const Player* player = playerProvider.getPlayer();
    EXPECT_EQ(player->getServerPlayerId(), replacementId);
    EXPECT_STREQ(player->getServerPlayerName(), "Cy");
    EXPECT_FALSE(player->hasTeam());
    EXPECT_STREQ(player->getRegistrationTeam().name, "");
    EXPECT_TRUE(player->getRegistrationAffiliations().empty());
}

inline void playerProviderClearDropsProfile() {
    PlayerProvider playerProvider;
    applyTestRegistration(playerProvider.getPlayer(), "11111111-2222-4333-8444-555555555555", "Bo");
    playerProvider.clear();

    EXPECT_FALSE(playerProvider.hasPlayer());
    EXPECT_STREQ(playerProvider.getPlayer()->getServerPlayerName(), "");
}

inline void playerLegacyShimMapsHelixAffiliation() {
    PlayerProvider playerProvider;
    applyTestRegistration(playerProvider.getPlayer(), "11111111-2222-4333-8444-555555555555", "Bo");
    EXPECT_EQ(playerProvider.getPlayer()->getAllegiance(), Allegiance::HELIX);
    EXPECT_EQ(playerProvider.getPlayer()->getAllegianceString(), "Helix");
}

inline void playerLegacyShimMapsHunterTeamName() {
    PlayerProvider playerProvider;
    Team team;
    IdGenerator::copyId(team.name, "Hunter");
    playerProvider.getPlayer()->applyRegistrationProfile(UUIDBytes(), "Demo", team, {});
    EXPECT_TRUE(playerProvider.getPlayer()->isHunter());
}

inline void playerLegacyShimFormatsServerIdAsUserId() {
    PlayerProvider playerProvider;
    applyTestRegistration(playerProvider.getPlayer(), "11111111-2222-4333-8444-555555555555", "Bo");
    EXPECT_EQ(playerProvider.getPlayer()->getUserID(), "11111111-2222-4333-8444-555555555555");
}
