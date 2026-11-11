#include "game/match.hpp"
#include "id-generator.hpp"
#include "device/drivers/logger.hpp"
#include <cstring>

Match::Match(const char* mid, const char* playerId, bool isHunter) {
    IdGenerator::copyId(matchId, mid);
    if (!UUIDBytes::parse(matchId, matchUuid)) {
        IdGenerator::uuidStringToBytes(matchId, matchUuid.bytes);
    }
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

size_t Match::serialize(uint8_t* buffer) const {
    size_t currentPos = 0;
    uint8_t playerIdBytes[PLAYER_ID_BINARY_SIZE];

    memcpy(buffer + currentPos, matchUuid.bytes, IdGenerator::UUID_BINARY_SIZE);
    currentPos += IdGenerator::UUID_BINARY_SIZE;

    // Serialize hunter id (4 raw bytes)
    memcpy(playerIdBytes, hunter, PLAYER_ID_BINARY_SIZE);
    memcpy(buffer + currentPos, playerIdBytes, PLAYER_ID_BINARY_SIZE);
    currentPos += PLAYER_ID_BINARY_SIZE;

    // Serialize bounty id (4 raw bytes)
    memcpy(playerIdBytes, bounty, PLAYER_ID_BINARY_SIZE);
    memcpy(buffer + currentPos, playerIdBytes, PLAYER_ID_BINARY_SIZE);
    currentPos += PLAYER_ID_BINARY_SIZE;

    const uint32_t hunterTime = static_cast<uint32_t>(hunterDrawTimeMs);
    const uint32_t bountyTime = static_cast<uint32_t>(bountyDrawTimeMs);
    memcpy(buffer + currentPos, &hunterTime, sizeof(hunterTime));
    currentPos += sizeof(hunterTime);
    memcpy(buffer + currentPos, &bountyTime, sizeof(bountyTime));
    currentPos += sizeof(bountyTime);

    memcpy(buffer + currentPos, hunterPlayerId.bytes, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(buffer + currentPos, bountyPlayerId.bytes, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(buffer + currentPos, hunterTeamId.bytes, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(buffer + currentPos, bountyTeamId.bytes, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(buffer + currentPos, winnerPlayerId.bytes, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;

    memcpy(buffer + currentPos, hunterMac, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;
    memcpy(buffer + currentPos, bountyMac, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;

    memcpy(buffer + currentPos, &occurredAtUnixMs, sizeof(int64_t));
    currentPos += sizeof(int64_t);

    return currentPos;
}

size_t Match::deserialize(const uint8_t* buffer) {
    size_t currentPos = 0;

    memcpy(matchUuid.bytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    matchUuid.format(matchId);
    currentPos += IdGenerator::UUID_BINARY_SIZE;

    // The wire field carries no terminator, so bound the read by the destination
    // the same way every other id copy does. copyId also clears the tail, which a
    // raw memcpy of the field would not.
    IdGenerator::copyId(hunter, reinterpret_cast<const char*>(buffer + currentPos));
    currentPos += PLAYER_ID_BINARY_SIZE;

    IdGenerator::copyId(bounty, reinterpret_cast<const char*>(buffer + currentPos));
    currentPos += PLAYER_ID_BINARY_SIZE;

    uint32_t hunterTime = 0;
    uint32_t bountyTime = 0;
    memcpy(&hunterTime, buffer + currentPos, sizeof(hunterTime));
    currentPos += sizeof(hunterTime);
    memcpy(&bountyTime, buffer + currentPos, sizeof(bountyTime));
    currentPos += sizeof(bountyTime);
    hunterDrawTimeMs = hunterTime;
    bountyDrawTimeMs = bountyTime;

    memcpy(hunterPlayerId.bytes, buffer + currentPos, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(bountyPlayerId.bytes, buffer + currentPos, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(hunterTeamId.bytes, buffer + currentPos, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(bountyTeamId.bytes, buffer + currentPos, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;
    memcpy(winnerPlayerId.bytes, buffer + currentPos, UUIDBytes::SIZE);
    currentPos += UUIDBytes::SIZE;

    memcpy(hunterMac, buffer + currentPos, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;
    memcpy(bountyMac, buffer + currentPos, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;

    memcpy(&occurredAtUnixMs, buffer + currentPos, sizeof(int64_t));
    currentPos += sizeof(int64_t);

    return currentPos;
}
