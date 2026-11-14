#include "utils/sha256.hpp"

#include "utils/secure-wipe.hpp"

#include <cstring>

namespace {

constexpr uint32_t ROUND_CONSTANTS[64] = {
0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr uint32_t INITIAL_STATE[8] = {
0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

// The length field takes the last 8 bytes of the final block.
constexpr size_t LENGTH_FIELD_OFFSET = SHA256_BLOCK_SIZE - 8;

uint32_t rotateRight(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

}  // namespace

Sha256::Sha256() { std::memcpy(state, INITIAL_STATE, sizeof(state)); }

void Sha256::update(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return;
    messageLength += length;

    if (pendingLength > 0) {
        size_t take = SHA256_BLOCK_SIZE - pendingLength;
        if (take > length) take = length;
        std::memcpy(pendingBlock + pendingLength, data, take);
        pendingLength += take;
        data += take;
        length -= take;
        if (pendingLength < SHA256_BLOCK_SIZE) return;
        processBlock(pendingBlock);
        pendingLength = 0;
    }

    while (length >= SHA256_BLOCK_SIZE) {
        processBlock(data);
        data += SHA256_BLOCK_SIZE;
        length -= SHA256_BLOCK_SIZE;
    }

    if (length > 0) {
        std::memcpy(pendingBlock, data, length);
        pendingLength = length;
    }
}

void Sha256::finish(uint8_t (&digest)[SHA256_DIGEST_SIZE]) {
    const uint64_t messageBits = messageLength * 8;

    pendingBlock[pendingLength++] = 0x80;
    if (pendingLength > LENGTH_FIELD_OFFSET) {
        std::memset(pendingBlock + pendingLength, 0, SHA256_BLOCK_SIZE - pendingLength);
        processBlock(pendingBlock);
        pendingLength = 0;
    }
    std::memset(pendingBlock + pendingLength, 0, LENGTH_FIELD_OFFSET - pendingLength);
    for (size_t i = 0; i < 8; ++i) {
        pendingBlock[LENGTH_FIELD_OFFSET + i] = static_cast<uint8_t>(messageBits >> (56 - 8 * i));
    }
    processBlock(pendingBlock);
    pendingLength = 0;

    for (size_t i = 0; i < 8; ++i) {
        digest[4 * i] = static_cast<uint8_t>(state[i] >> 24);
        digest[4 * i + 1] = static_cast<uint8_t>(state[i] >> 16);
        digest[4 * i + 2] = static_cast<uint8_t>(state[i] >> 8);
        digest[4 * i + 3] = static_cast<uint8_t>(state[i]);
    }

    // The state is the digest and the pending block the end of the message,
    // either of which a caller may need kept secret.
    secureWipe(this, sizeof(*this));
}

void Sha256::processBlock(const uint8_t* block) {
    uint32_t schedule[64];
    for (size_t i = 0; i < 16; ++i) {
        schedule[i] = (static_cast<uint32_t>(block[4 * i]) << 24) |
                      (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
                      (static_cast<uint32_t>(block[4 * i + 2]) << 8) |
                      static_cast<uint32_t>(block[4 * i + 3]);
    }
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t small0 = rotateRight(schedule[i - 15], 7) ^
                                rotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
        const uint32_t small1 = rotateRight(schedule[i - 2], 17) ^
                                rotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + small0 + schedule[i - 7] + small1;
    }

    uint32_t working[8];
    std::memcpy(working, state, sizeof(working));
    uint32_t& a = working[0];
    uint32_t& b = working[1];
    uint32_t& c = working[2];
    uint32_t& d = working[3];
    uint32_t& e = working[4];
    uint32_t& f = working[5];
    uint32_t& g = working[6];
    uint32_t& h = working[7];

    for (size_t i = 0; i < 64; ++i) {
        const uint32_t big1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + big1 + choice + ROUND_CONSTANTS[i] + schedule[i];
        const uint32_t big0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = big0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    for (size_t i = 0; i < 8; ++i) {
        state[i] += working[i];
    }

    // Both are computed from the block, which a caller may need kept secret.
    secureWipe(schedule, sizeof(schedule));
    secureWipe(working, sizeof(working));
}
