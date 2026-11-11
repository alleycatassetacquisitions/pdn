#include "game/match.hpp"
#include "id-generator.hpp"
#include "device/drivers/logger.hpp"
#include <cstring>

Match::Match(const char* mid, const char* playerId, bool isHunter) {
    IdGenerator::copyId(match_id, mid);
    if (isHunter) {
        IdGenerator::copyId(hunter, playerId);
    } else {
        IdGenerator::copyId(bounty, playerId);
    }
}

void Match::setHunterId(const char* hunterId) {
    IdGenerator::copyId(hunter, hunterId);
}

void Match::setBountyId(const char* bountyId) {
    IdGenerator::copyId(bounty, bountyId);
}

void Match::setHunterDrawTime(unsigned long timeMs) {
    hunter_draw_time_ms = timeMs;
}

void Match::setBountyDrawTime(unsigned long timeMs) {
    bounty_draw_time_ms = timeMs;
}

size_t Match::serialize(uint8_t* buffer) const {
    size_t currentPos = 0;
    uint8_t uuidBytes[IdGenerator::UUID_BINARY_SIZE];
    uint8_t playerIdBytes[PLAYER_ID_BINARY_SIZE];

    // Serialize match_id
    IdGenerator::uuidStringToBytes(match_id, uuidBytes);
    memcpy(buffer + currentPos, uuidBytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;

    // Serialize hunter id (4 raw bytes)
    memcpy(playerIdBytes, hunter, PLAYER_ID_BINARY_SIZE);
    memcpy(buffer + currentPos, playerIdBytes, PLAYER_ID_BINARY_SIZE);
    currentPos += PLAYER_ID_BINARY_SIZE;

    // Serialize bounty id (4 raw bytes)
    memcpy(playerIdBytes, bounty, PLAYER_ID_BINARY_SIZE);
    memcpy(buffer + currentPos, playerIdBytes, PLAYER_ID_BINARY_SIZE);
    currentPos += PLAYER_ID_BINARY_SIZE;

    // Serialize draw times
    memcpy(buffer + currentPos, &hunter_draw_time_ms, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    memcpy(buffer + currentPos, &bounty_draw_time_ms, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);

    return currentPos;
}

size_t Match::deserialize(const uint8_t* buffer) {
    size_t currentPos = 0;
    uint8_t uuidBytes[IdGenerator::UUID_BINARY_SIZE];

    // Deserialize match_id
    memcpy(uuidBytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    {
        std::string tmp = IdGenerator::uuidBytesToString(uuidBytes);
        IdGenerator::copyId(match_id, tmp.c_str());
    }
    currentPos += IdGenerator::UUID_BINARY_SIZE;

    // The wire field carries no terminator, so bound the read by the destination
    // the same way every other id copy does. copyId also clears the tail, which a
    // raw memcpy of the field would not.
    IdGenerator::copyId(hunter, reinterpret_cast<const char*>(buffer + currentPos));
    currentPos += PLAYER_ID_BINARY_SIZE;

    IdGenerator::copyId(bounty, reinterpret_cast<const char*>(buffer + currentPos));
    currentPos += PLAYER_ID_BINARY_SIZE;

    // Deserialize draw times
    memcpy(&hunter_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);
    memcpy(&bounty_draw_time_ms, buffer + currentPos, sizeof(unsigned long));
    currentPos += sizeof(unsigned long);

    return currentPos;
}