#include "utils/hmac-sha256.hpp"

#include "device/drivers/logger.hpp"
#include "utils/secure-wipe.hpp"

#include <cstring>

namespace {

constexpr const char* HMAC_SHA256_TAG = "HMAC";

constexpr uint8_t INNER_PAD = 0x36;
constexpr uint8_t OUTER_PAD = 0x5c;

}  // namespace

bool computeHmacSha256(const uint8_t* key, size_t keyLength, const uint8_t* message,
                       size_t messageLength, uint8_t (&tag)[HMAC_SHA256_SIZE]) {
    if (key == nullptr || keyLength == 0 || (message == nullptr && messageLength > 0)) {
        LOG_E(HMAC_SHA256_TAG, "no key, or a null message with a length; no tag computed");
        std::memset(tag, 0, sizeof(tag));
        return false;
    }

    // Holds the hashed key first and the inner digest after it.
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint8_t keyBlock[SHA256_BLOCK_SIZE] = {};

    if (keyLength > SHA256_BLOCK_SIZE) {
        Sha256 keyHash;
        keyHash.update(key, keyLength);
        keyHash.finish(digest);
        std::memcpy(keyBlock, digest, sizeof(digest));
    } else {
        std::memcpy(keyBlock, key, keyLength);
    }

    for (size_t i = 0; i < sizeof(keyBlock); ++i) {
        keyBlock[i] ^= INNER_PAD;
    }
    Sha256 inner;
    inner.update(keyBlock, sizeof(keyBlock));
    inner.update(message, messageLength);
    inner.finish(digest);

    // The block is the key under the inner pad; this leaves it under the outer one.
    for (size_t i = 0; i < sizeof(keyBlock); ++i) {
        keyBlock[i] ^= INNER_PAD ^ OUTER_PAD;
    }
    Sha256 outer;
    outer.update(keyBlock, sizeof(keyBlock));
    outer.update(digest, sizeof(digest));
    outer.finish(tag);

    secureWipe(keyBlock, sizeof(keyBlock));
    secureWipe(digest, sizeof(digest));
    return true;
}
