#pragma once

#include <cstddef>
#include <cstdint>

/**
 * A UUID held as its 16 raw bytes. The 36-character text form exists only at
 * serialization boundaries. All zeros (the nil UUID) means "no id".
 */
struct UUIDBytes {
    static constexpr size_t SIZE = 16;
    static constexpr size_t TEXT_LENGTH = 36;
    static constexpr size_t TEXT_BUFFER_SIZE = TEXT_LENGTH + 1;

    uint8_t bytes[SIZE] = {};

    /**
     * Parses 8-4-4-4-12 hex text in either case. Checks shape only, not version or variant.
     *
     * @return false, leaving @p out unchanged, when @p text is null or not UUID-shaped.
     */
    static bool parse(const char* text, UUIDBytes& out);

    /** Writes lowercase 8-4-4-4-12 text and a terminator into @p text. */
    void format(char (&text)[TEXT_BUFFER_SIZE]) const;

    /** @return true when every byte is zero. */
    bool isZero() const;

    /** @return true when both hold the same 16 bytes. */
    bool operator==(const UUIDBytes& other) const;

    /** @return true when the 16 bytes differ. */
    bool operator!=(const UUIDBytes& other) const;
};
