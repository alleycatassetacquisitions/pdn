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

// A record check hashes the record and then the trailer, so the digest must
// not depend on where the pieces fall against the 64-byte block.
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

// Expected values are the first 8 bytes of SHA-256 over record bytes then the
// 17 trailer bytes, computed with an independent implementation.
inline void recordCheckIsFirstEightBytesOfSha256OverRecordThenTrailer() {
    uint8_t crashRecord[CRASH_RECORD_SIZE];
    for (size_t i = 0; i < sizeof(crashRecord); ++i) {
        crashRecord[i] = static_cast<uint8_t>(i);
    }
    const RecordTrailer noRecordKey{};
    uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(crashRecord, sizeof(crashRecord), noRecordKey, recordCheck);
    EXPECT_EQ(toHexText(recordCheck, sizeof(recordCheck)), "1ea6486a623913d7");

    uint8_t matchRecord[MATCH_RECORD_SIZE];
    for (size_t i = 0; i < sizeof(matchRecord); ++i) {
        matchRecord[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    RecordTrailer trailer{};
    trailer.keyId = 5;
    for (size_t i = 0; i < sizeof(trailer.tag); ++i) {
        trailer.tag[i] = static_cast<uint8_t>(0xA0 + i);
    }
    computeRecordCheck(matchRecord, sizeof(matchRecord), trailer, recordCheck);
    EXPECT_EQ(toHexText(recordCheck, sizeof(recordCheck)), "ee02c639aefa7fc3");
}

inline void recordCheckChangesWithTheTrailer() {
    uint8_t crashRecord[CRASH_RECORD_SIZE] = {};
    RecordTrailer trailer{};
    uint8_t underNoKey[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(crashRecord, sizeof(crashRecord), trailer, underNoKey);

    trailer.keyId = 1;
    uint8_t underKeyOne[RECORD_CHECK_SIZE] = {};
    computeRecordCheck(crashRecord, sizeof(crashRecord), trailer, underKeyOne);
    EXPECT_NE(toHexText(underNoKey, sizeof(underNoKey)),
              toHexText(underKeyOne, sizeof(underKeyOne)))
    << "the same record sent under another key ID must not share a check";
}
