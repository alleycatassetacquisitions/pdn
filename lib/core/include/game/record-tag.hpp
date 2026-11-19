#pragma once

#include "game/match.hpp"
#include "game/record-key.hpp"

/**
 * Writes the first RECORD_TAG_SIZE bytes of HMAC-SHA-256 under @p recordKey over
 * @p tagged's record bytes into its tag, leaving no copy of the rest of the HMAC.
 * With @p recordKey null or holding no key the tag is all zeros.
 * @return true when the tag was computed under a held key.
 */
bool tagMatchRecord(const RecordKey* recordKey, TaggedMatchRecord& tagged);
