#pragma once

#include <gtest/gtest.h>

#include <cstring>

#include "device/remote-device-coordinator.hpp"
#include "game/player-provider.hpp"
#include "game/player-types.hpp"
#include "utils/uuid-bytes.hpp"

// The record key registerTestPlayer() holds counts up from this byte unless the
// caller names another. From 0x00 it is the key of the test vector in the
// record-authentication spec.
constexpr uint8_t TEST_RECORD_KEY_FIRST_BYTE = 0x00;

/** A record key counting up from @p firstByte, so two keys are told apart. */
inline ApplicationPayload makeTestRecordKey(uint8_t firstByte) {
    ApplicationPayload key{};
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(firstByte + i);
    }
    return key;
}

/**
 * Registers @p playerId on @p playerProvider and sets a record key counting up
 * from @p keyFirstByte; a PDN stores a match only while it holds a key, so
 * every test that expects a stored match registers through here.
 */
inline void registerTestPlayer(PlayerProvider& playerProvider, const char* playerId,
                               const char* name, const char* teamName,
                               uint8_t keyFirstByte = TEST_RECORD_KEY_FIRST_BYTE) {
    UUIDBytes serverId{};
    ASSERT_TRUE(UUIDBytes::parse(playerId, serverId)) << playerId;
    Team team{};
    strncpy(team.name, teamName, sizeof(team.name) - 1);
    playerProvider.getPlayer()->applyRegistrationProfile(serverId, name, team, {});

    playerProvider.getRecordKey()->set(makeTestRecordKey(keyFirstByte).data());
}
