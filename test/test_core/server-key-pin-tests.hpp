#pragma once

#include <gtest/gtest.h>

#include <cctype>
#include <cstring>
#include <string>

#include "sha256-tests.hpp"
#include "utils/server-key-pin.hpp"

// DER SubjectPublicKeyInfo of a P-256 key from a throwaway self-signed
// certificate, and its pin as printed by the pipeline in
// wifi_credentials.ini.example (openssl x509 -pubkey -noout | openssl pkey
// -pubin -outform DER | openssl dgst -sha256).
inline const uint8_t TEST_SERVER_SPKI[] = {
0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0xc6, 0x4f, 0xb1, 0xf3, 0xe0,
0x2b, 0x58, 0x64, 0x51, 0x06, 0xba, 0x30, 0x21, 0xa5, 0x7f, 0xdf, 0x14, 0x64, 0xd2, 0x24, 0x56,
0x3c, 0xa2, 0xef, 0x1f, 0xb6, 0x83, 0xa5, 0xc9, 0x93, 0x5a, 0xfc, 0xa0, 0x19, 0x8f, 0x66, 0x87,
0xd6, 0x2c, 0xe2, 0x4e, 0x0b, 0xa5, 0xd2, 0x2f, 0xbb, 0x52, 0xce, 0x1c, 0xd6, 0x50, 0x4d, 0xd0,
0x09, 0xb9, 0xe3, 0x89, 0xc2, 0x88, 0xec, 0xed, 0x8e, 0xf4, 0x85,
};
inline const char TEST_SERVER_KEY_PIN[] =
"31117c29ddde650b0b0d82e017a64496f79bae219b8799a0b9729cb46569e221";

// Catches a hash over the wrong bytes or a parse that misreads the hex: the
// pin openssl prints for a key must match that key, in either case, and the
// hash logged for a presented key is the one a pin is set to.
inline void serverKeyPinTrustsTheKeyItWasComputedFrom() {
    char presented[SERVER_KEY_PIN_HEX_LENGTH + 1] = {};
    const ServerKeyPin pin(TEST_SERVER_KEY_PIN);
    ASSERT_TRUE(pin.isSet());
    EXPECT_TRUE(pin.matchesKey(TEST_SERVER_SPKI, sizeof(TEST_SERVER_SPKI), presented));
    EXPECT_STREQ(presented, TEST_SERVER_KEY_PIN)
    << "the logged hash is not one a pin can be set to";

    std::string upperCase = TEST_SERVER_KEY_PIN;
    for (char& digit : upperCase) {
        digit = static_cast<char>(toupper(static_cast<unsigned char>(digit)));
    }
    EXPECT_TRUE(
    ServerKeyPin(upperCase).matchesKey(TEST_SERVER_SPKI, sizeof(TEST_SERVER_SPKI), presented))
    << "an upper-case pin is the same pin";
}

// Catches a comparison that stops short of the last byte or ignores a bit:
// one bit off, in the pin or in the presented key, is another key.
inline void serverKeyPinRefusesAKeyOneBitAway() {
    char presented[SERVER_KEY_PIN_HEX_LENGTH + 1] = {};
    std::string lastBitFlipped = TEST_SERVER_KEY_PIN;
    lastBitFlipped.back() = '0';  // ...221 becomes ...220
    EXPECT_FALSE(
    ServerKeyPin(lastBitFlipped).matchesKey(TEST_SERVER_SPKI, sizeof(TEST_SERVER_SPKI), presented));
    EXPECT_STREQ(presented, TEST_SERVER_KEY_PIN) << "a refused key's hash is not logged";

    std::string firstBitFlipped = TEST_SERVER_KEY_PIN;
    firstBitFlipped.front() = 'b';  // 3... becomes b...
    EXPECT_FALSE(ServerKeyPin(firstBitFlipped)
                 .matchesKey(TEST_SERVER_SPKI, sizeof(TEST_SERVER_SPKI), presented));

    uint8_t otherKey[sizeof(TEST_SERVER_SPKI)];
    memcpy(otherKey, TEST_SERVER_SPKI, sizeof(otherKey));
    otherKey[sizeof(otherKey) - 1] ^= 0x01;
    EXPECT_FALSE(
    ServerKeyPin(TEST_SERVER_KEY_PIN).matchesKey(otherKey, sizeof(otherKey), presented));
    EXPECT_FALSE(ServerKeyPin(TEST_SERVER_KEY_PIN).matchesKey(TEST_SERVER_SPKI, 0, presented))
    << "an empty key matched";
}

// Catches a pin that half-parses: a malformed pin is unset, and an unset pin
// trusts nothing but still reports the presented key's hash for the log.
inline void serverKeyPinThatIsMalformedTrustsNothing() {
    char presented[SERVER_KEY_PIN_HEX_LENGTH + 1] = {};
    const std::string pin = TEST_SERVER_KEY_PIN;
    std::string notHex = pin;
    notHex[10] = 'g';
    std::string spaced = pin;
    spaced[0] = ' ';
    const std::string cases[] = {"",        pin.substr(0, SERVER_KEY_PIN_HEX_LENGTH - 1),
                                 pin + "1", notHex,
                                 spaced,    "SHA2-256(stdin)= " + pin};
    for (const std::string& text : cases) {
        const ServerKeyPin malformed(text);
        EXPECT_FALSE(malformed.isSet()) << "\"" << text << "\" parsed as a pin";
        presented[0] = '\0';
        EXPECT_FALSE(malformed.matchesKey(TEST_SERVER_SPKI, sizeof(TEST_SERVER_SPKI), presented))
        << "\"" << text << "\" trusted a key";
        EXPECT_STREQ(presented, TEST_SERVER_KEY_PIN)
        << "\"" << text << "\" left the presented key unlogged";
    }
    EXPECT_FALSE(ServerKeyPin().matchesKey(TEST_SERVER_SPKI, sizeof(TEST_SERVER_SPKI), presented));
    EXPECT_FALSE(ServerKeyPin(TEST_SERVER_KEY_PIN).matchesKey(nullptr, 4, presented))
    << "no bytes matched";
    EXPECT_STREQ(presented, "") << "a hash was logged for no bytes";
}
