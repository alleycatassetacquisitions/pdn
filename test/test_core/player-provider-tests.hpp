#pragma once

#include <gtest/gtest.h>

#include "game/player-provider.hpp"
#include "game/player-types.hpp"
#include "id-generator.hpp"
#include "uuid-bytes-tests.hpp"

inline PlayerV2 makeTestPlayer(const char* playerId, const char* name) {
    PlayerV2 player;
    player.id = makeTestUuid(playerId);
    IdGenerator::copyId(player.name, name);
    player.team.id = makeTestUuid("5a4b3c2d-1e0f-4a9b-8c7d-6e5f4a3b2c1d");
    IdGenerator::copyId(player.team.name, "Hunters");
    player.affiliations.push_back({});
    player.affiliations[0].categoryId = makeTestUuid("c0ffee00-0000-4000-8000-000000000001");
    IdGenerator::copyId(player.affiliations[0].categoryName, "Allegiance");
    player.affiliations[0].affiliationId = makeTestUuid("c0ffee00-0000-4000-8000-000000000002");
    IdGenerator::copyId(player.affiliations[0].affiliationName, "Helix");
    return player;
}

inline void playerProviderStartsEmpty() {
    PlayerProvider playerProvider;
    EXPECT_FALSE(playerProvider.hasPlayer());
}

inline void playerProviderReturnsDefaultWhenEmpty() {
    PlayerProvider playerProvider;
    const PlayerV2& player = playerProvider.getPlayer();
    EXPECT_TRUE(player.id.isNil());
    EXPECT_STREQ(player.name, "");
    EXPECT_FALSE(player.hasTeam());
    EXPECT_TRUE(player.affiliations.empty());
}

inline void playerProviderStoresProfile() {
    PlayerV2 source = makeTestPlayer("11111111-2222-4333-8444-555555555555", "Bo");
    PlayerProvider playerProvider;
    playerProvider.setPlayer(source);

    ASSERT_TRUE(playerProvider.hasPlayer());
    const PlayerV2& player = playerProvider.getPlayer();
    EXPECT_EQ(player.id, source.id);
    EXPECT_STREQ(player.name, "Bo");
    EXPECT_EQ(player.team.id, source.team.id);
    EXPECT_STREQ(player.team.name, "Hunters");
    ASSERT_EQ(player.affiliations.size(), 1u);
    EXPECT_STREQ(player.affiliations[0].affiliationName, "Helix");
}

inline void playerProviderOverwriteReplacesProfile() {
    PlayerProvider playerProvider;
    playerProvider.setPlayer(makeTestPlayer("11111111-2222-4333-8444-555555555555", "Bo"));

    PlayerV2 replacement;
    replacement.id = makeTestUuid("66666666-7777-4888-8999-aaaaaaaaaaaa");
    IdGenerator::copyId(replacement.name, "Cy");
    playerProvider.setPlayer(replacement);

    const PlayerV2& player = playerProvider.getPlayer();
    EXPECT_EQ(player.id, replacement.id);
    EXPECT_STREQ(player.name, "Cy");
    EXPECT_FALSE(player.hasTeam());
    EXPECT_STREQ(player.team.name, "");
    EXPECT_TRUE(player.affiliations.empty());
}

inline void playerProviderClearDropsProfile() {
    PlayerProvider playerProvider;
    playerProvider.setPlayer(makeTestPlayer("11111111-2222-4333-8444-555555555555", "Bo"));
    playerProvider.clear();

    EXPECT_FALSE(playerProvider.hasPlayer());
    EXPECT_STREQ(playerProvider.getPlayer().name, "");
}
