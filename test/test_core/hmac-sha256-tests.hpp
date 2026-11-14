#pragma once

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "sha256-tests.hpp"
#include "utils/hmac-sha256.hpp"

/** HMAC-SHA-256 of @p message under @p key, as lowercase hex. */
inline std::string hmacSha256Hex(const std::string& key, const std::string& message) {
    uint8_t tag[HMAC_SHA256_SIZE] = {};
    EXPECT_TRUE(computeHmacSha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                                  reinterpret_cast<const uint8_t*>(message.data()), message.size(),
                                  tag));
    return toHexText(tag, sizeof(tag));
}

// RFC 4231 test cases 1 to 4: keys shorter than the 64-byte block.
inline void hmacSha256MatchesTheRfc4231VectorsForShortKeys() {
    EXPECT_EQ(hmacSha256Hex(std::string(20, '\x0b'), "Hi There"),
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    EXPECT_EQ(hmacSha256Hex("Jefe", "what do ya want for nothing?"),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    EXPECT_EQ(hmacSha256Hex(std::string(20, '\xaa'), std::string(50, '\xdd')),
              "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");

    std::string countingKey;
    for (char value = 0x01; value <= 0x19; ++value) {
        countingKey += value;
    }
    EXPECT_EQ(hmacSha256Hex(countingKey, std::string(50, '\xcd')),
              "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
}

// RFC 4231 test cases 6 and 7: a 131-byte key, which is hashed before use.
inline void hmacSha256MatchesTheRfc4231VectorsForKeysLongerThanABlock() {
    const std::string longKey(131, '\xaa');
    EXPECT_EQ(hmacSha256Hex(longKey, "Test Using Larger Than Block-Size Key - Hash Key First"),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    EXPECT_EQ(hmacSha256Hex(longKey,
                            "This is a test using a larger than block-size key and a larger "
                            "than block-size data. The key needs to be hashed before being "
                            "used by the HMAC algorithm."),
              "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

// A key of exactly one block is used as it is and one byte more is hashed
// first; the RFC vectors sit far from that edge. The keys count up from 0x00
// and the tags come from Python's hmac.
inline void hmacSha256HashesTheKeyOnlyWhenItIsLongerThanABlock() {
    std::string countingKey;
    for (size_t value = 0; value < SHA256_BLOCK_SIZE; ++value) {
        countingKey += static_cast<char>(value);
    }
    EXPECT_EQ(hmacSha256Hex(countingKey, "what do ya want for nothing?"),
              "5431cc41830bee7889a6b5d04b33877387ea9b8170759f4dca4323cfb5725508");

    countingKey += static_cast<char>(SHA256_BLOCK_SIZE);
    EXPECT_EQ(hmacSha256Hex(countingKey, "what do ya want for nothing?"),
              "b8510ec6c86f16d7c86061bd02266a93ffa7add59683bd03dc645ba1f238c38c");
}

// The device's messages are never empty, but nothing in HMAC forbids one. The
// tag comes from Python's hmac.
inline void hmacSha256TagsAnEmptyMessage() {
    EXPECT_EQ(hmacSha256Hex("Jefe", ""),
              "923598ca6d64af2a5dba79dcd021a8a0fe5c5f557519adaaf0ad532d4506dd30");

    const uint8_t key[] = {'J', 'e', 'f', 'e'};
    uint8_t tag[HMAC_SHA256_SIZE] = {};
    EXPECT_TRUE(computeHmacSha256(key, sizeof(key), nullptr, 0, tag));
    EXPECT_EQ(toHexText(tag, sizeof(tag)),
              "923598ca6d64af2a5dba79dcd021a8a0fe5c5f557519adaaf0ad532d4506dd30")
    << "null with no length is the empty message";
}

// A key that failed to load must not yield the tag of the empty key, which
// anyone can compute, nor leave the caller's stale bytes to be sent as one.
inline void hmacSha256RefusesAMissingKeyOrANullMessageThatHasALength() {
    const uint8_t message[] = {'H', 'i'};
    const uint8_t key[] = {'J', 'e', 'f', 'e'};
    const std::string allZero(2 * HMAC_SHA256_SIZE, '0');

    uint8_t tag[HMAC_SHA256_SIZE];
    memset(tag, 0xEE, sizeof(tag));
    EXPECT_FALSE(computeHmacSha256(nullptr, sizeof(key), message, sizeof(message), tag));
    EXPECT_EQ(toHexText(tag, sizeof(tag)), allZero);

    memset(tag, 0xEE, sizeof(tag));
    EXPECT_FALSE(computeHmacSha256(key, 0, message, sizeof(message), tag));
    EXPECT_EQ(toHexText(tag, sizeof(tag)), allZero);

    memset(tag, 0xEE, sizeof(tag));
    EXPECT_FALSE(computeHmacSha256(nullptr, 0, message, sizeof(message), tag));
    EXPECT_EQ(toHexText(tag, sizeof(tag)), allZero);

    memset(tag, 0xEE, sizeof(tag));
    EXPECT_FALSE(computeHmacSha256(key, sizeof(key), nullptr, sizeof(message), tag));
    EXPECT_EQ(toHexText(tag, sizeof(tag)), allZero);
}
