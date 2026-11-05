#pragma once

#include <gtest/gtest.h>
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device/drivers/unknown-storage-namespace-exception.hpp"
#include "game/match-manager.hpp"
#include "device-mock.hpp"

inline void storageUnknownNamespaceThrowsOnWrite() {
    NativePrefsDriver storage("test-storage", {MATCHES_PREFS_NAMESPACE});
    EXPECT_THROW(storage.write("not_registered", "key", "value"), UnknownStorageNamespaceException);
}

inline void storageRegisteredNamespaceRoundTripsString() {
    NativePrefsDriver storage("test-storage", {MATCHES_PREFS_NAMESPACE});
    const std::string value = "{\"match\":true}";
    ASSERT_EQ(storage.write(MATCHES_PREFS_NAMESPACE, "match_0", value), value.size());
    EXPECT_EQ(storage.read(MATCHES_PREFS_NAMESPACE, "match_0", ""), value);
}

inline void storageUnknownNamespaceThrowsOnRead() {
    NativePrefsDriver storage("test-storage", {MATCHES_PREFS_NAMESPACE});
    EXPECT_THROW(storage.read("bad_ns", "key", "default"), UnknownStorageNamespaceException);
}

inline void storageUCharRoundTripInRegisteredNamespace() {
    NativePrefsDriver storage("test-storage", {MATCHES_PREFS_NAMESPACE});
    ASSERT_EQ(storage.writeUChar(MATCHES_PREFS_NAMESPACE, "count", 42), 1u);
    EXPECT_EQ(storage.readUChar(MATCHES_PREFS_NAMESPACE, "count", 0), 42);
}

inline void storageMatchStoreCapsWithoutLosingRecords() {
    NativePrefsDriver storage("ring-storage", {MATCHES_PREFS_NAMESPACE});
    Player player;
    char playerId[] = "ring";
    player.setUserID(playerId);
    player.setIsHunter(true);

    MatchManager manager(nullptr);
    manager.initialize(&player, &storage);

    const size_t capacity = 32;
    const size_t overflow = 5;
    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    for (size_t i = 0; i < capacity + overflow; i++) {
        manager.initializeMatch(opponentMac);
        manager.setHunterDrawTime(static_cast<long>(100 + i));
        manager.setBountyDrawTime(static_cast<long>(900 - i));
        ASSERT_TRUE(manager.finalizeMatch()) << "save " << i << " refused";
    }

    ASSERT_EQ(manager.getStoredMatchCount(), capacity);

    for (size_t slot = 0; slot < capacity; slot++) {
        const size_t save = (slot < overflow) ? slot + capacity : slot;
        Match* stored = manager.readMatchFromStorage(static_cast<uint8_t>(slot));
        ASSERT_NE(stored, nullptr) << "slot " << slot << " empty";
        EXPECT_EQ(stored->getHunterDrawTime(), 100u + save) << "slot " << slot;
        delete stored;
    }

    EXPECT_EQ(manager.readMatchFromStorage(capacity), nullptr);
}

inline void storageClampsAMatchCountLeftByOlderFirmware() {
    NativePrefsDriver storage("legacy-storage", {MATCHES_PREFS_NAMESPACE});
    ASSERT_EQ(storage.writeUChar(MATCHES_PREFS_NAMESPACE, "count", 40), 1u);

    Player player;
    char playerId[] = "legacy";
    player.setUserID(playerId);
    player.setIsHunter(true);

    MatchManager manager(nullptr);
    manager.initialize(&player, &storage);

    EXPECT_EQ(manager.getStoredMatchCount(), 32u);

    uint8_t opponentMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    manager.initializeMatch(opponentMac);
    manager.setHunterDrawTime(1);
    manager.setBountyDrawTime(2);
    ASSERT_TRUE(manager.finalizeMatch());

    EXPECT_EQ(manager.getStoredMatchCount(), 32u);
}
