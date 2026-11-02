#include "device/drivers/logger.hpp"
#include "game/match.hpp"
#include "id-generator.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

class FuzzNullLogger : public LoggerInterface {
public:
    void vlog(LogLevel, const char*, const char*, int, const char*, va_list) override {}
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static FuzzNullLogger nullLogger;
    g_logger = &nullLogger;

    // 1. Fuzz Match::fromJson with arbitrary string input
    std::string inputStr(reinterpret_cast<const char*>(data), size);
    Match jsonMatch;
    jsonMatch.fromJson(inputStr);

    std::string reserializedJson = jsonMatch.toJson();
    uint8_t binaryOut[MATCH_BINARY_SIZE] = {};
    size_t written = jsonMatch.serialize(binaryOut);
    if (written != MATCH_BINARY_SIZE) {
        __builtin_trap();
    }

    // 2. Fuzz IdGenerator::uuidStringToBytes with arbitrary string input
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

    // 3. Fuzz Match::deserialize with an exact-sized heap allocation so ASan
    // immediately catches any out-of-bounds read past MATCH_BINARY_SIZE.
    if (size >= MATCH_BINARY_SIZE) {
        auto* exactBuf = new uint8_t[MATCH_BINARY_SIZE];
        std::memcpy(exactBuf, data, MATCH_BINARY_SIZE);

        Match binMatch;
        size_t bytesRead = binMatch.deserialize(exactBuf);
        if (bytesRead != MATCH_BINARY_SIZE) {
            __builtin_trap();
        }

        // Verify null-termination invariants on all string fields
        if (std::strlen(binMatch.getMatchId()) >= IdGenerator::UUID_BUFFER_SIZE) {
            __builtin_trap();
        }
        if (std::strlen(binMatch.getHunterId()) > PLAYER_ID_BINARY_SIZE) {
            __builtin_trap();
        }
        if (std::strlen(binMatch.getBountyId()) > PLAYER_ID_BINARY_SIZE) {
            __builtin_trap();
        }

        // Invariant: deserialize -> toJson -> fromJson must preserve all fields
        std::string binToJson = binMatch.toJson();
        Match roundTripMatch;
        roundTripMatch.fromJson(binToJson);

        if (std::strcmp(binMatch.getMatchId(), roundTripMatch.getMatchId()) != 0 ||
            std::strcmp(binMatch.getHunterId(), roundTripMatch.getHunterId()) != 0 ||
            std::strcmp(binMatch.getBountyId(), roundTripMatch.getBountyId()) != 0 ||
            binMatch.getHunterDrawTime() != roundTripMatch.getHunterDrawTime() ||
            binMatch.getBountyDrawTime() != roundTripMatch.getBountyDrawTime()) {
            __builtin_trap();
        }

        // Invariant: re-serializing and re-deserializing must preserve all fields
        uint8_t roundTripBuf[MATCH_BINARY_SIZE] = {};
        roundTripMatch.serialize(roundTripBuf);
        Match secondBinMatch;
        secondBinMatch.deserialize(roundTripBuf);

        if (std::strcmp(binMatch.getMatchId(), secondBinMatch.getMatchId()) != 0 ||
            std::strcmp(binMatch.getHunterId(), secondBinMatch.getHunterId()) != 0 ||
            std::strcmp(binMatch.getBountyId(), secondBinMatch.getBountyId()) != 0 ||
            binMatch.getHunterDrawTime() != secondBinMatch.getHunterDrawTime() ||
            binMatch.getBountyDrawTime() != secondBinMatch.getBountyDrawTime()) {
            __builtin_trap();
        }

        delete[] exactBuf;
    }

    return 0;
}
