#pragma once

#include <cstddef>
#include <cstdint>

#include "game/match.hpp"
#include "game/record-key.hpp"

constexpr size_t RECORD_TAG_SIZE = 16;

/// A match record as a PDN stores and relays it: the 112 record bytes, then their tag.
struct TaggedMatchRecord {
    MatchRecord record;
    uint8_t tag[RECORD_TAG_SIZE];
} __attribute__((packed));
static_assert(sizeof(TaggedMatchRecord) == 128, "a ring slot holds the record, then its tag");

/**
 * Writes the first RECORD_TAG_SIZE bytes of HMAC-SHA-256 under @p recordKey over
 * @p tagged's record bytes into its tag, leaving no copy of the rest of the HMAC.
 * With @p recordKey null or holding no key the tag is all zeros.
 * @return true when the tag was computed under a held key.
 */
bool tagMatchRecord(const RecordKey* recordKey, TaggedMatchRecord& tagged);
