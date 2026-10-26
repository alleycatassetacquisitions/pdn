#pragma once

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "device/drivers/storage-interface.hpp"
#include "game/node-network/match-storage.hpp"
#include "uuid-bytes-tests.hpp"

/** In-memory StorageInterface that keeps strings byte-exact and records which namespaces were used. */
class InMemoryStorage : public StorageInterface {
public:
    size_t write(const std::string& ns, const std::string& key, const std::string& value) override {
        usedNamespaces.insert(ns);
        if (failWrites) {
            return 0;
        }
        strings[ns + "/" + key] = value;
        return value.size();
    }

    std::string read(const std::string& ns, const std::string& key,
                     const std::string& defaultValue) override {
        std::map<std::string, std::string>::const_iterator found = strings.find(ns + "/" + key);
        return found == strings.end() ? defaultValue : found->second;
    }

    bool remove(const std::string& ns, const std::string& key) override {
        usedNamespaces.insert(ns);
        return strings.erase(ns + "/" + key) > 0;
    }

    bool clear(const std::string& ns) override {
        usedNamespaces.insert(ns);
        const std::string prefix = ns + "/";
        for (std::map<std::string, std::string>::iterator it = strings.begin(); it != strings.end();) {
            it = it->first.rfind(prefix, 0) == 0 ? strings.erase(it) : std::next(it);
        }
        for (std::map<std::string, uint8_t>::iterator it = bytes.begin(); it != bytes.end();) {
            it = it->first.rfind(prefix, 0) == 0 ? bytes.erase(it) : std::next(it);
        }
        return true;
    }

    void end() override {}

    uint8_t readUChar(const std::string& ns, const std::string& key, uint8_t defaultValue) override {
        std::map<std::string, uint8_t>::const_iterator found = bytes.find(ns + "/" + key);
        return found == bytes.end() ? defaultValue : found->second;
    }

    size_t writeUChar(const std::string& ns, const std::string& key, uint8_t value) override {
        usedNamespaces.insert(ns);
        if (failWrites) {
            return 0;
        }
        bytes[ns + "/" + key] = value;
        return 1;
    }

    bool failWrites = false;
    std::set<std::string> usedNamespaces;
    std::map<std::string, std::string> strings;
    std::map<std::string, uint8_t> bytes;
};

inline QuickdrawMatchRecord makeStorageTestRecord(uint8_t seed) {
    QuickdrawMatchRecord record;
    for (size_t index = 0; index < UUIDBytes::SIZE; index++) {
        record.matchId.bytes[index] = static_cast<uint8_t>(seed + index);
        record.playerAId.bytes[index] = static_cast<uint8_t>(seed + 20 + index);
        record.playerBId.bytes[index] = static_cast<uint8_t>(seed + 40 + index);
        record.playerATeamId.bytes[index] = static_cast<uint8_t>(seed + 60 + index);
        record.playerBTeamId.bytes[index] = static_cast<uint8_t>(seed + 80 + index);
    }
    record.winnerPlayerId = record.playerAId;
    record.playerADrawTimeMs = 200u + seed;
    record.playerBDrawTimeMs = 300u + seed;
    record.occurredAtUnixMs = 1700000000000 + seed;
    return record;
}

inline void expectRecordsEqual(const QuickdrawMatchRecord& expected, const QuickdrawMatchRecord& actual) {
    EXPECT_EQ(expected.matchId, actual.matchId);
    EXPECT_EQ(expected.playerAId, actual.playerAId);
    EXPECT_EQ(expected.playerBId, actual.playerBId);
    EXPECT_EQ(expected.playerATeamId, actual.playerATeamId);
    EXPECT_EQ(expected.playerBTeamId, actual.playerBTeamId);
    EXPECT_EQ(expected.winnerPlayerId, actual.winnerPlayerId);
    EXPECT_EQ(expected.playerADrawTimeMs, actual.playerADrawTimeMs);
    EXPECT_EQ(expected.playerBDrawTimeMs, actual.playerBDrawTimeMs);
    EXPECT_EQ(expected.occurredAtUnixMs, actual.occurredAtUnixMs);
}

inline void matchStorageStartsEmpty() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);

    EXPECT_EQ(storage.getCount(), 0u);
}

inline void matchStorageRoundTripsRecord() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    const QuickdrawMatchRecord original = makeStorageTestRecord(3);

    ASSERT_TRUE(storage.append(original));

    EXPECT_EQ(storage.getCount(), 1u);
    QuickdrawMatchRecord loaded;
    ASSERT_TRUE(storage.readAt(0, loaded));
    expectRecordsEqual(original, loaded);
}

inline void matchStorageRoundTripsZeroBytes() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    QuickdrawMatchRecord original;
    original.matchId.bytes[0] = 1;
    original.playerAId.bytes[0] = 2;
    original.playerBId.bytes[0] = 3;
    original.winnerPlayerId = original.playerAId;

    ASSERT_TRUE(storage.append(original));

    QuickdrawMatchRecord loaded;
    ASSERT_TRUE(storage.readAt(0, loaded));
    expectRecordsEqual(original, loaded);
    EXPECT_TRUE(loaded.playerATeamId.isZero());
    EXPECT_EQ(loaded.playerADrawTimeMs, 0u);
}

inline void matchStorageKeepsAppendOrder() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    ASSERT_TRUE(storage.append(makeStorageTestRecord(1)));
    ASSERT_TRUE(storage.append(makeStorageTestRecord(2)));

    EXPECT_EQ(storage.getCount(), 2u);
    QuickdrawMatchRecord first;
    QuickdrawMatchRecord second;
    ASSERT_TRUE(storage.readAt(0, first));
    ASSERT_TRUE(storage.readAt(1, second));
    expectRecordsEqual(makeStorageTestRecord(1), first);
    expectRecordsEqual(makeStorageTestRecord(2), second);
}

inline void matchStorageCountSurvivesReopen() {
    InMemoryStorage backing;
    {
        MatchStorage storage(&backing);
        ASSERT_TRUE(storage.append(makeStorageTestRecord(5)));
    }

    MatchStorage reopened(&backing);

    EXPECT_EQ(reopened.getCount(), 1u);
}

inline void matchStorageClearRemovesRecords() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    ASSERT_TRUE(storage.append(makeStorageTestRecord(1)));
    ASSERT_TRUE(storage.append(makeStorageTestRecord(2)));

    storage.clear();

    EXPECT_EQ(storage.getCount(), 0u);
    QuickdrawMatchRecord loaded;
    EXPECT_FALSE(storage.readAt(0, loaded));
}

inline void matchStorageRejectsAppendAtCapacity() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    for (size_t index = 0; index < MatchStorage::MAX_PENDING_RECORDS; index++) {
        ASSERT_TRUE(storage.append(makeStorageTestRecord(static_cast<uint8_t>(index))));
    }

    EXPECT_FALSE(storage.append(makeStorageTestRecord(200)));
    EXPECT_EQ(storage.getCount(), MatchStorage::MAX_PENDING_RECORDS);
}

inline void matchStorageDoesNotCountFailedWrite() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    backing.failWrites = true;

    EXPECT_FALSE(storage.append(makeStorageTestRecord(1)));

    backing.failWrites = false;
    EXPECT_EQ(storage.getCount(), 0u);
}

inline void matchStorageReadRejectsOutOfRange() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    ASSERT_TRUE(storage.append(makeStorageTestRecord(1)));

    QuickdrawMatchRecord loaded;
    EXPECT_FALSE(storage.readAt(1, loaded));
}

inline void matchStorageReadRejectsTruncatedRecord() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    ASSERT_TRUE(storage.append(makeStorageTestRecord(1)));
    for (std::map<std::string, std::string>::iterator it = backing.strings.begin();
         it != backing.strings.end(); ++it) {
        if (it->second.size() > 8) {
            it->second.resize(8);
        }
    }

    QuickdrawMatchRecord loaded;
    EXPECT_FALSE(storage.readAt(0, loaded));
}

inline void matchStorageUsesOwnNamespace() {
    InMemoryStorage backing;
    MatchStorage storage(&backing);
    ASSERT_TRUE(storage.append(makeStorageTestRecord(1)));
    storage.clear();

    EXPECT_EQ(backing.usedNamespaces.count("matches"), 0u);
    EXPECT_EQ(backing.usedNamespaces.size(), 1u);
}
