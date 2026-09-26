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

// The match store is a ring: a save never fails for lack of room, the oldest
// record is overwritten instead, and reads come back oldest-first. Before this
// was a ring the store jammed permanently once full, and nothing noticed
// because finalizeMatch's return is discarded at its only call site.
inline void storageMatchRingOverwritesOldestAndNeverFills() {
    NativePrefsDriver storage("ring-storage", {MATCHES_PREFS_NAMESPACE});
    Player player;
    char playerId[] = "ring";
    player.setUserID(playerId);
    player.setIsHunter(true);

    FakeQuickdrawWirelessManager wireless;
    MatchManager manager;
    manager.initialize(&player, &storage, &wireless);

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

    // The oldest retained record is the first that was not overwritten.
    Match* oldest = manager.readMatchFromStorage(0);
    ASSERT_NE(oldest, nullptr);
    EXPECT_EQ(oldest->getHunterDrawTime(), 100u + overflow);
    delete oldest;

    Match* newest = manager.readMatchFromStorage(capacity - 1);
    ASSERT_NE(newest, nullptr);
    EXPECT_EQ(newest->getHunterDrawTime(), 100u + capacity + overflow - 1);
    delete newest;

    EXPECT_EQ(manager.readMatchFromStorage(capacity), nullptr);
}
