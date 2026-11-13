#include "wireless/record-relay-types.hpp"

#include <cstring>

#include "utils/sha256.hpp"

void computeRecordCheck(const uint8_t* recordBytes, size_t recordLength,
                        const RecordTrailer& trailer, uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
    Sha256 hash;
    hash.update(recordBytes, recordLength);
    hash.update(reinterpret_cast<const uint8_t*>(&trailer), sizeof(trailer));
    uint8_t digest[SHA256_DIGEST_SIZE];
    hash.finish(digest);
    std::memcpy(recordCheck, digest, RECORD_CHECK_SIZE);
}
