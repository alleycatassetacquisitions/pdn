#include "game/record-tag.hpp"

#include "utils/hmac-sha256.hpp"
#include "utils/secure-wipe.hpp"

#include <cstring>

static_assert(RECORD_TAG_SIZE <= HMAC_SHA256_SIZE, "a record tag is a prefix of the HMAC");

bool tagMatchRecord(const RecordKey* recordKey, TaggedMatchRecord& tagged) {
    // Stays zero without a key; computeHmacSha256 also zeroes it when it fails.
    uint8_t hmac[HMAC_SHA256_SIZE] = {};
    const bool keyed = recordKey != nullptr && recordKey->isHeld() &&
                       computeHmacSha256(recordKey->getBytes(), RECORD_KEY_SIZE,
                                         reinterpret_cast<const uint8_t*>(&tagged.record),
                                         sizeof(tagged.record), hmac);
    memcpy(tagged.tag, hmac, RECORD_TAG_SIZE);
    secureWipe(hmac, sizeof(hmac));
    return keyed;
}
