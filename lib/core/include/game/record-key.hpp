#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "utils/secure-wipe.hpp"

constexpr size_t RECORD_KEY_SIZE = 32;

/** Holds the player's record key for the session, in memory only; read it through getBytes() at the
 * moment of use and keep no copy. */
class RecordKey {
public:
    /** Starts with no key held. */
    RecordKey() = default;

    /** Overwrites the key so it does not outlive its holder in freed memory. */
    ~RecordKey() { clear(); }

    /** Not copyable: a copy would be a second place the key lives, which one clear() cannot reach.
     */
    RecordKey(const RecordKey&) = delete;
    /** Not assignable, for the same reason. */
    RecordKey& operator=(const RecordKey&) = delete;

    /** @return true while a key is held. */
    bool isHeld() const { return held; }

    /** @return the RECORD_KEY_SIZE key bytes; meaningful only while isHeld(). */
    const uint8_t* getBytes() const { return bytes; }

    /** Holds @p key, replacing any key already held. */
    void set(const uint8_t key[RECORD_KEY_SIZE]) {
        memcpy(bytes, key, RECORD_KEY_SIZE);
        held = true;
    }

    /** Drops the key and overwrites its bytes. */
    void clear() {
        secureWipe(bytes, RECORD_KEY_SIZE);
        held = false;
    }

private:
    uint8_t bytes[RECORD_KEY_SIZE] = {};
    bool held = false;
};
