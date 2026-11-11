#pragma once

#include <gtest/gtest.h>
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device/drivers/unknown-storage-namespace-exception.hpp"
#include "game/match-manager.hpp"

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

inline void storageUIntAndBytesRoundTripInRegisteredNamespace() {
    NativePrefsDriver storage("test-storage", {MATCHES_PREFS_NAMESPACE});
    ASSERT_EQ(storage.writeUInt(MATCHES_PREFS_NAMESPACE, "seq", 123456u), sizeof(uint32_t));
    EXPECT_EQ(storage.readUInt(MATCHES_PREFS_NAMESPACE, "seq", 0u), 123456u);

    const uint8_t rawBytes[8] = {0x00, 0x11, 0x00, 0x22, 0x33, 0x00, 0x44, 0x55};
    uint8_t restored[8] = {};
    ASSERT_EQ(storage.writeBytes(MATCHES_PREFS_NAMESPACE, "blob", rawBytes, sizeof(rawBytes)),
              sizeof(rawBytes));
    ASSERT_EQ(storage.readBytes(MATCHES_PREFS_NAMESPACE, "blob", restored, sizeof(restored)),
              sizeof(restored));
    EXPECT_EQ(memcmp(rawBytes, restored, sizeof(rawBytes)), 0);
}
