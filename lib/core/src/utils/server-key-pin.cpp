#include "utils/server-key-pin.hpp"

#include "device/drivers/logger.hpp"

#include <cstring>

namespace {

constexpr const char* SERVER_KEY_PIN_TAG = "SERVER_KEY_PIN";

// The value of one hex digit, or -1 for any other character.
int hexDigitValue(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

void formatDigest(const uint8_t (&digest)[SHA256_DIGEST_SIZE],
                  char (&hexText)[SERVER_KEY_PIN_HEX_LENGTH + 1]) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        hexText[2 * i] = HEX_DIGITS[digest[i] >> 4];
        hexText[2 * i + 1] = HEX_DIGITS[digest[i] & 0x0F];
    }
    hexText[SERVER_KEY_PIN_HEX_LENGTH] = '\0';
}

}  // namespace

ServerKeyPin::ServerKeyPin(const std::string& hexText) {
    if (hexText.size() != SERVER_KEY_PIN_HEX_LENGTH) return;
    uint8_t parsed[SHA256_DIGEST_SIZE] = {};
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        const int high = hexDigitValue(hexText[2 * i]);
        const int low = hexDigitValue(hexText[2 * i + 1]);
        if (high < 0 || low < 0) return;
        parsed[i] = static_cast<uint8_t>((high << 4) | low);
    }
    std::memcpy(pinnedDigest, parsed, sizeof(pinnedDigest));
    pinIsSet = true;
}

bool ServerKeyPin::matchesKey(const uint8_t* spki, size_t length,
                              char (&presentedKeyHash)[SERVER_KEY_PIN_HEX_LENGTH + 1]) const {
    if (spki == nullptr && length > 0) {
        LOG_E(SERVER_KEY_PIN_TAG, "No key bytes to match");
        presentedKeyHash[0] = '\0';
        return false;
    }
    uint8_t digest[SHA256_DIGEST_SIZE];
    Sha256 hash;
    hash.update(spki, length);
    hash.finish(digest);
    formatDigest(digest, presentedKeyHash);

    uint8_t difference = 0;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        difference |= static_cast<uint8_t>(digest[i] ^ pinnedDigest[i]);
    }
    return pinIsSet && difference == 0;
}
