#include "game/record-tag.hpp"

#include "utils/hmac-sha256.hpp"
#include "utils/secure-wipe.hpp"

#include <cstring>

static_assert(RECORD_TAG_SIZE <= HMAC_SHA256_SIZE, "a record tag is a prefix of the HMAC output");

bool tagMatchRecord(const RecordKey* recordKey, MatchRecordContent& content) {
    if (recordKey == nullptr || !recordKey->isHeld()) {
        return false;
    }

    uint8_t hmacOutput[HMAC_SHA256_SIZE] = {};
    const bool computed = computeHmacSha256(recordKey->getBytes(), RECORD_KEY_SIZE,
                                            content.recordBytes, MATCH_RECORD_SIZE, hmacOutput);
    if (computed) {
        memcpy(content.tag, hmacOutput, RECORD_TAG_SIZE);
    }
    // The bytes past the tag are HMAC output nobody is meant to see.
    secureWipe(hmacOutput, sizeof(hmacOutput));
    return computed;
}
