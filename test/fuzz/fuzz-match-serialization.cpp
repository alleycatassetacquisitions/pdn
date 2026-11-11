#include "fuzz-common.hpp"
#include "game/match.hpp"
#include "id-generator.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string inputStr(reinterpret_cast<const char*>(data), size);
    Match stringMatch(inputStr.c_str(), inputStr.c_str(), true);
    uint8_t binaryOut[MATCH_BINARY_SIZE] = {};
    size_t written = stringMatch.serialize(binaryOut);
    if (written != MATCH_BINARY_SIZE) {
        __builtin_trap();
    }

    uint8_t uuidBytes[IdGenerator::UUID_BINARY_SIZE] = {};
    IdGenerator::uuidStringToBytes(inputStr, uuidBytes);
    std::string uuidRoundTrip = IdGenerator::uuidBytesToString(uuidBytes);
    if (uuidRoundTrip.size() != IdGenerator::UUID_STRING_LENGTH) {
        __builtin_trap();
    }

    // Invariant: uuidBytesToString -> uuidStringToBytes must be an exact identity
    uint8_t uuidBytesSecond[IdGenerator::UUID_BINARY_SIZE] = {};
    IdGenerator::uuidStringToBytes(uuidRoundTrip, uuidBytesSecond);
    if (std::memcmp(uuidBytes, uuidBytesSecond, IdGenerator::UUID_BINARY_SIZE) != 0) {
        __builtin_trap();
    }

    // Exact-sized heap buffer so ASan catches reads past MATCH_BINARY_SIZE.
    if (size >= MATCH_BINARY_SIZE) {
        ExactBuffer exactBuf(data, MATCH_BINARY_SIZE);

        Match binMatch;
        size_t bytesRead = binMatch.deserialize(exactBuf.data());
        if (bytesRead != MATCH_BINARY_SIZE) {
            __builtin_trap();
        }

        assertMatchStringBounds(binMatch);

        // Invariant: re-serializing and re-deserializing must preserve all fields
        uint8_t roundTripBuf[MATCH_BINARY_SIZE] = {};
        binMatch.serialize(roundTripBuf);
        Match secondBinMatch;
        secondBinMatch.deserialize(roundTripBuf);

        if (std::strcmp(binMatch.getMatchId(), secondBinMatch.getMatchId()) != 0 ||
            std::strcmp(binMatch.getHunterId(), secondBinMatch.getHunterId()) != 0 ||
            std::strcmp(binMatch.getBountyId(), secondBinMatch.getBountyId()) != 0 ||
            binMatch.getHunterDrawTime() != secondBinMatch.getHunterDrawTime() ||
            binMatch.getBountyDrawTime() != secondBinMatch.getBountyDrawTime()) {
            __builtin_trap();
        }
    }

    return 0;
}
