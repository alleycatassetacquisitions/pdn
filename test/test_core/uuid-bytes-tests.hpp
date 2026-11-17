#pragma once

#include <gtest/gtest.h>
#include <string>

#include "utils/uuid-bytes.hpp"

inline UUIDBytes makeTestUuid(const char* text) {
    UUIDBytes id;
    EXPECT_TRUE(UUIDBytes::parse(text, id)) << text;
    return id;
}

inline void uuidBytesParsesKnownValue() {
    UUIDBytes id;
    ASSERT_TRUE(UUIDBytes::parse("123e4567-e89b-12d3-a456-426614174000", id));

    const uint8_t expected[UUIDBytes::SIZE] = {
        0x12, 0x3e, 0x45, 0x67, 0xe8, 0x9b, 0x12, 0xd3,
        0xa4, 0x56, 0x42, 0x66, 0x14, 0x17, 0x40, 0x00};
    for (size_t index = 0; index < UUIDBytes::SIZE; index++) {
        EXPECT_EQ(id.bytes[index], expected[index]) << "byte " << index;
    }
}

inline void uuidBytesAcceptsEitherCase() {
    EXPECT_EQ(makeTestUuid("ABCDEF01-2345-4ABC-8DEF-0123456789AB"),
              makeTestUuid("abcdef01-2345-4abc-8def-0123456789ab"));
}

inline void uuidBytesFormatsLowercaseRoundTrip() {
    char text[UUIDBytes::TEXT_BUFFER_SIZE];
    makeTestUuid("ABCDEF01-2345-4ABC-8DEF-0123456789AB").format(text);

    EXPECT_STREQ(text, "abcdef01-2345-4abc-8def-0123456789ab");
}

inline void uuidBytesRejectsMalformedText() {
    const UUIDBytes original = makeTestUuid("11111111-1111-4111-8111-111111111111");
    const std::string valid = "22222222-2222-4222-8222-222222222222";
    const std::string malformed[] = {
        "",
        valid.substr(0, UUIDBytes::TEXT_LENGTH - 1),
        valid + "2",
        "222222222-222-4222-8222-222222222222",
        "22222222-2222-4222-8222-22222222222g",
        "22222222_2222_4222_8222_222222222222",
        "SHT-00000000000000000000000000000003",
    };

    for (const std::string& text : malformed) {
        UUIDBytes id = original;
        EXPECT_FALSE(UUIDBytes::parse(text.c_str(), id)) << '"' << text << '"';
        EXPECT_EQ(id, original) << "output changed for \"" << text << '"';
    }

    UUIDBytes id = original;
    EXPECT_FALSE(UUIDBytes::parse(nullptr, id));
    EXPECT_EQ(id, original);
}

inline void uuidBytesReportsNil() {
    EXPECT_TRUE(UUIDBytes().isZero());
    EXPECT_TRUE(makeTestUuid("00000000-0000-0000-0000-000000000000").isZero());
    EXPECT_FALSE(makeTestUuid("00000000-0000-0000-0000-000000000001").isZero());
}

inline void uuidBytesComparesByValue() {
    const UUIDBytes first = makeTestUuid("11111111-1111-4111-8111-111111111111");
    const UUIDBytes second = makeTestUuid("11111111-1111-4111-8111-111111111112");

    EXPECT_TRUE(first == makeTestUuid("11111111-1111-4111-8111-111111111111"));
    EXPECT_TRUE(first != second);
}
