#pragma once

#include "game/record-key.hpp"
#include "wireless/record-relay-types.hpp"

/**
 * Writes into @p content's tag the first RECORD_TAG_SIZE bytes of
 * HMAC-SHA-256(@p recordKey, @p content's record bytes), leaving no copy of the
 * key or of the rest of the HMAC.
 * @return false, with the tag untouched, when no key is held; such a record
 * must not be stored or sent.
 */
[[nodiscard]] bool tagMatchRecord(const RecordKey* recordKey, MatchRecordContent& content);
