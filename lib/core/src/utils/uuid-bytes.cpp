#include "utils/uuid-bytes.hpp"
#include <cstring>

namespace {

bool isHyphenPosition(size_t position) {
    return position == 8 || position == 13 || position == 18 || position == 23;
}

int hexValue(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

}  // namespace

bool UUIDBytes::parse(const char* text, UUIDBytes& out) {
    if (text == nullptr) {
        return false;
    }

    // A short string stops at its terminator, which is neither hex nor a hyphen,
    // so nothing past it is read.
    UUIDBytes parsed;
    size_t byteIndex = 0;
    for (size_t position = 0; position < TEXT_LENGTH; position++) {
        if (isHyphenPosition(position)) {
            if (text[position] != '-') {
                return false;
            }
            continue;
        }
        const int highNibble = hexValue(text[position]);
        if (highNibble < 0) {
            return false;
        }
        const int lowNibble = hexValue(text[++position]);
        if (lowNibble < 0) {
            return false;
        }
        parsed.bytes[byteIndex++] = static_cast<uint8_t>((highNibble << 4) | lowNibble);
    }
    if (text[TEXT_LENGTH] != '\0') {
        return false;
    }

    out = parsed;
    return true;
}

void UUIDBytes::format(char (&text)[TEXT_BUFFER_SIZE]) const {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    size_t position = 0;
    for (size_t index = 0; index < SIZE; index++) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            text[position++] = '-';
        }
        text[position++] = HEX_DIGITS[bytes[index] >> 4];
        text[position++] = HEX_DIGITS[bytes[index] & 0x0F];
    }
    text[TEXT_LENGTH] = '\0';
}

bool UUIDBytes::isNil() const {
    for (uint8_t byte : bytes) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

bool UUIDBytes::operator==(const UUIDBytes& other) const {
    return memcmp(bytes, other.bytes, SIZE) == 0;
}

bool UUIDBytes::operator!=(const UUIDBytes& other) const {
    return !(*this == other);
}
