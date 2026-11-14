#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "utils/sha256.hpp"
#include "wireless/record-relay-types.hpp"

/** Lowercase hex of @p length bytes. */
inline std::string toHexText(const uint8_t* bytes, size_t length) {
    std::string text;
    char pair[3];
    for (size_t i = 0; i < length; ++i) {
        snprintf(pair, sizeof(pair), "%02x", bytes[i]);
        text += pair;
    }
    return text;
}

/** SHA-256 of @p message fed in pieces of @p pieceSize bytes, as lowercase hex. */
inline std::string sha256HexInPieces(const std::string& message, size_t pieceSize) {
    Sha256 hash;
    for (size_t offset = 0; offset < message.size(); offset += pieceSize) {
        const size_t take = std::min(pieceSize, message.size() - offset);
        hash.update(reinterpret_cast<const uint8_t*>(message.data()) + offset, take);
    }
    uint8_t digest[SHA256_DIGEST_SIZE];
    hash.finish(digest);
    return toHexText(digest, sizeof(digest));
}

// FIPS 180-4 example messages.
inline void sha256MatchesPublishedVectors() {
    EXPECT_EQ(sha256HexInPieces("", 64),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256HexInPieces("abc", 64),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256HexInPieces("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 64),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

inline void sha256MatchesTheMillionAVector() {
    const std::string millionA(1000000, 'a');
    EXPECT_EQ(sha256HexInPieces(millionA, millionA.size()),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// The padding changes shape where the message ends against the 64-byte block:
// 55 bytes is the longest that pads within its block, 56 the shortest that
// needs another, and 119 is the same edge one block later. The messages are
// that many 'a' bytes; the digests come from Python's hashlib.sha256.
inline void sha256MatchesDigestsAtThePaddingBoundaries() {
    EXPECT_EQ(sha256HexInPieces(std::string(55, 'a'), 64),
              "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    EXPECT_EQ(sha256HexInPieces(std::string(56, 'a'), 64),
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT_EQ(sha256HexInPieces(std::string(63, 'a'), 64),
              "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    EXPECT_EQ(sha256HexInPieces(std::string(64, 'a'), 64),
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    EXPECT_EQ(sha256HexInPieces(std::string(119, 'a'), 119),
              "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
}

// A message that ends in a refilled buffer: the first piece leaves bytes in
// the pending block that the padding has to clear. 120 'a' bytes go in as
// 60 + 60 and 119 as 64 + 55; the digests come from Python's hashlib.sha256
// and must equal the same message fed in one piece.
inline void sha256PadsOverABufferThatWasFilledBefore() {
    const std::string message120(120, 'a');
    EXPECT_EQ(sha256HexInPieces(message120, 60),
              "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c");
    EXPECT_EQ(sha256HexInPieces(message120, 60), sha256HexInPieces(message120, 120));

    const std::string message119(119, 'a');
    EXPECT_EQ(sha256HexInPieces(message119, 64),
              "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
    EXPECT_EQ(sha256HexInPieces(message119, 64), sha256HexInPieces(message119, 119));
}

inline void sha256IsTheSameHoweverTheMessageIsSplit() {
    const std::string message = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (size_t pieceSize : {1u, 3u, 55u, 56u, 63u}) {
        EXPECT_EQ(sha256HexInPieces(message, pieceSize),
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")
        << "piece size " << pieceSize;
    }
    const std::string millionA(1000000, 'a');
    EXPECT_EQ(sha256HexInPieces(millionA, 129),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Expected values come from an independent implementation (Python's hashlib).
inline void recordCheckOfACrashRecordIsFirstEightBytesOfSha256OverItsBytes() {
    CrashRecordContent crash{};
    for (size_t i = 0; i < sizeof(crash.recordBytes); ++i) {
        crash.recordBytes[i] = static_cast<uint8_t>(i);
    }
    uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(crash, recordCheck);
    EXPECT_EQ(toHexText(recordCheck, sizeof(recordCheck)), "40a1ed73b46030c8");
}

inline void recordCheckOfAMatchRecordIsFirstEightBytesOfSha256OverItsBytesThenItsTag() {
    MatchRecordContent match{};
    for (size_t i = 0; i < sizeof(match.recordBytes); ++i) {
        match.recordBytes[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    for (size_t i = 0; i < sizeof(match.tag); ++i) {
        match.tag[i] = static_cast<uint8_t>(0xA0 + i);
    }
    uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(match, recordCheck);
    EXPECT_EQ(toHexText(recordCheck, sizeof(recordCheck)), "b1f38a5347c2f8d0");

    // An all-zero tag is hashed like any other.
    memset(match.tag, 0, sizeof(match.tag));
    computeRecordCheck(match, recordCheck);
    EXPECT_EQ(toHexText(recordCheck, sizeof(recordCheck)), "f9b3e7a3bdb564a7");

    uint8_t bytesOnly[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(match.recordBytes, sizeof(match.recordBytes), bytesOnly);
    EXPECT_EQ(toHexText(bytesOnly, sizeof(bytesOnly)), "96b928cff8528dbb")
    << "the 112 record bytes alone hash to something else";
}

inline void recordCheckChangesWithTheTag() {
    MatchRecordContent match{};
    uint8_t withAZeroTag[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(match, withAZeroTag);

    match.tag[RECORD_TAG_SIZE - 1] = 1;
    uint8_t withAnotherTag[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(match, withAnotherTag);
    EXPECT_NE(toHexText(withAZeroTag, sizeof(withAZeroTag)),
              toHexText(withAnotherTag, sizeof(withAnotherTag)))
    << "the same match record under another tag must not share a check";
}
