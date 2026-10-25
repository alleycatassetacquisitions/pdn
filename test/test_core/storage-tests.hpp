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
