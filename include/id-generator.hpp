#pragma once

#include <UUID.h>
#include <string>

class IdGenerator {
public:
    // UUID string format: 8-4-4-4-12 chars + 4 hyphens + null terminator = 37
    static constexpr size_t UUID_STRING_LENGTH = 36;  // Length without null terminator
    static constexpr size_t UUID_BUFFER_SIZE = 37;   // Length with null terminator
    static constexpr size_t UUID_BINARY_SIZE = 16;   // Size of binary UUID in bytes

    static IdGenerator *GetInstance() {
        static IdGenerator instance;
        return &instance;
    };

    char *generateId() {
        generator.generate();
        return generator.toCharArray();
    };

    /**
     * Converts a UUID string to its binary representation.
     * 
     * A UUID string looks like: "123e4567-e89b-12d3-a456-426614174000"
     * Format: 8 chars - 4 chars - 4 chars - 4 chars - 12 chars
     * 
     * @param uuid The UUID string to convert
     * @param bytes Output buffer for the binary data (must be 16 bytes)
     */
    static void uuidStringToBytes(const std::string& uuid, uint8_t* bytes) {
        int byteIndex = 0;
        for (size_t i = 0; i < uuid.length(); i++) {
            if (uuid[i] == '-') continue;  // Skip hyphens in UUID string
            
            // Take two hex chars and combine them into one byte
            uint8_t highNibble = hexCharToInt(uuid[i++]);  // First char -> upper 4 bits
            uint8_t lowNibble = hexCharToInt(uuid[i]);     // Second char -> lower 4 bits
            bytes[byteIndex++] = (highNibble << 4) | lowNibble;  // Combine into one byte
        }
    }

    /**
     * Converts a binary UUID (16 bytes) back to its string representation.
     * 
     * Full UUID format:
     * 8 chars - 4 chars - 4 chars - 4 chars - 12 chars
     * 
     * @param bytes The 16-byte binary UUID
     * @return The formatted UUID string
     */
    static std::string uuidBytesToString(const uint8_t* bytes) {
        char uuid[37];  // 36 chars + null terminator
        int pos = 0;
        
        for (int i = 0; i < 16; i++) {
            // Add hyphens according to UUID format
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                uuid[pos++] = '-';
            }
            
            // Convert each byte to two hex characters
            byteToHexChars(bytes[i], &uuid[pos]);
            pos += 2;
        }
        uuid[36] = '\0';  // Null terminate the string
        return std::string(uuid);
    }

private:
    UUID generator;

    IdGenerator() {
        generator.setVariant4Mode();
        generator.seed(random(999999999), random(999999999));
    };

    /**
     * Converts a single hexadecimal character to its numeric value (0-15).
     */
    static uint8_t hexCharToInt(char c) {
        if (c >= '0' && c <= '9') return c - '0';           // Convert '0'-'9' to 0-9
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;      // Convert 'a'-'f' to 10-15
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;      // Convert 'A'-'F' to 10-15
        return 0;  // Return 0 for invalid characters
    }

    /**
     * Converts a byte value to two hexadecimal characters.
     */
    static void byteToHexChars(uint8_t byte, char* chars) {
        const char hex[] = "0123456789abcdef";  // Lookup table for hex digits
        chars[0] = hex[byte >> 4];      // Convert high nibble (upper 4 bits)
        chars[1] = hex[byte & 0xF];     // Convert low nibble (lower 4 bits)
    }
};
