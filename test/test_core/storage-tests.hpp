#pragma once

#include <gtest/gtest.h>
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device/drivers/nvs-partition-range.hpp"
#include "device/drivers/unknown-storage-namespace-exception.hpp"
#include "game/match-manager.hpp"

inline void storageUnknownNamespaceThrowsOnWrite() {
    NativePrefsDriver storage("test-storage", {MATCH_RING_PREFS_NAMESPACE});
    EXPECT_THROW(storage.write("not_registered", "key", "value"), UnknownStorageNamespaceException);
}

inline void storageRegisteredNamespaceRoundTripsString() {
    NativePrefsDriver storage("test-storage", {MATCH_RING_PREFS_NAMESPACE});
    const std::string value = "{\"match\":true}";
    ASSERT_EQ(storage.write(MATCH_RING_PREFS_NAMESPACE, "match_0", value), value.size());
    EXPECT_EQ(storage.read(MATCH_RING_PREFS_NAMESPACE, "match_0", ""), value);
}

inline void storageUnknownNamespaceThrowsOnRead() {
    NativePrefsDriver storage("test-storage", {MATCH_RING_PREFS_NAMESPACE});
    EXPECT_THROW(storage.read("bad_ns", "key", "default"), UnknownStorageNamespaceException);
}

inline void storageUCharRoundTripInRegisteredNamespace() {
    NativePrefsDriver storage("test-storage", {MATCH_RING_PREFS_NAMESPACE});
    ASSERT_EQ(storage.writeUChar(MATCH_RING_PREFS_NAMESPACE, "count", 42), 1u);
    EXPECT_EQ(storage.readUChar(MATCH_RING_PREFS_NAMESPACE, "count", 0), 42);
}

inline void storageUIntAndBytesRoundTripInRegisteredNamespace() {
    NativePrefsDriver storage("test-storage", {MATCH_RING_PREFS_NAMESPACE});
    ASSERT_EQ(storage.writeUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 123456u), sizeof(uint32_t));
    EXPECT_EQ(storage.readUInt(MATCH_RING_PREFS_NAMESPACE, "seq", 0u), 123456u);

    const uint8_t rawBytes[8] = {0x00, 0x11, 0x00, 0x22, 0x33, 0x00, 0x44, 0x55};
    uint8_t restored[8] = {};
    ASSERT_EQ(storage.writeBytes(MATCH_RING_PREFS_NAMESPACE, "blob", rawBytes, sizeof(rawBytes)),
              sizeof(rawBytes));
    ASSERT_EQ(storage.readBytes(MATCH_RING_PREFS_NAMESPACE, "blob", restored, sizeof(restored)),
              sizeof(restored));
    EXPECT_EQ(memcmp(rawBytes, restored, sizeof(rawBytes)), 0);
}

// The match ring's range as the PDN gives it, and the source each table yields.
inline constexpr NvsPartitionRange TEST_RING_RANGE = {"records", 0x7E1000, 0x1F000};

TEST(NvsPartitionSourceTests, rangeNotInTheTableIsRegistered) {
    EXPECT_EQ(chooseNvsPartitionSource(false, 0, 0, &TEST_RING_RANGE),
              NvsPartitionSource::REGISTER);
}

TEST(NvsPartitionSourceTests, tableListingTheRangeIsUsed) {
    EXPECT_EQ(chooseNvsPartitionSource(true, 0x7E1000, 0x1F000, &TEST_RING_RANGE),
              NvsPartitionSource::TABLE);
}

// Either field off would put test-table devices on a range no deployed device uses.
TEST(NvsPartitionSourceTests, tableListingTheLabelElsewhereIsRefused) {
    EXPECT_EQ(chooseNvsPartitionSource(true, 0x7E0000, 0x1F000, &TEST_RING_RANGE),
              NvsPartitionSource::NONE);
    EXPECT_EQ(chooseNvsPartitionSource(true, 0x7E1000, 0x1E000, &TEST_RING_RANGE),
              NvsPartitionSource::NONE);
}

// The default partition has no range: only the table can hold it.
TEST(NvsPartitionSourceTests, partitionWithoutARangeComesOnlyFromTheTable) {
    EXPECT_EQ(chooseNvsPartitionSource(true, 0x9000, 0x5000, nullptr), NvsPartitionSource::TABLE);
    EXPECT_EQ(chooseNvsPartitionSource(false, 0, 0, nullptr), NvsPartitionSource::NONE);
}
