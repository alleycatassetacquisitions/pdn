#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t SHA256_DIGEST_SIZE = 32;
constexpr size_t SHA256_BLOCK_SIZE = 64;

/// SHA-256 (FIPS 180-4), fed in pieces; portable because the native build has no
/// platform crypto library.
class Sha256 {
public:
    /** Starts an empty message. */
    Sha256();

    /** Appends @p length bytes of @p data to the message. */
    void update(const uint8_t* data, size_t length);

    /** Pads the message and writes its digest; the object is spent afterwards. */
    void finish(uint8_t (&digest)[SHA256_DIGEST_SIZE]);

private:
    void processBlock(const uint8_t* block);

    uint32_t state[8];
    uint64_t messageLength = 0;
    uint8_t pendingBlock[SHA256_BLOCK_SIZE] = {};
    size_t pendingLength = 0;
};
