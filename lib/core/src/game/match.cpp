#include "game/match.hpp"
#include "id-generator.hpp"
#include "device/drivers/logger.hpp"
#include <ArduinoJson.h>
#include <cstring>

Match::Match(const char* mid, const char* playerId, bool isHunter) {
    IdGenerator::copyId(match_id, mid);
    if (!UUIDBytes::parse(match_id, matchUuid)) {
        IdGenerator::uuidStringToBytes(match_id, matchUuid.bytes);
    }
    if (isHunter) {
        setHunterId(playerId);
    } else {
        setBountyId(playerId);
    }
}

void Match::setMatchUuid(const UUIDBytes& uuid) {
    matchUuid = uuid;
    uuid.format(match_id);
}

void Match::setHunterId(const char* hunterId) {
    IdGenerator::copyId(hunter, hunterId);
    UUIDBytes parsed{};
    if (UUIDBytes::parse(hunterId, parsed)) {
        hunterPlayerId = parsed;
    }
}

void Match::setBountyId(const char* bountyId) {
    IdGenerator::copyId(bounty, bountyId);
    UUIDBytes parsed{};
    if (UUIDBytes::parse(bountyId, parsed)) {
        bountyPlayerId = parsed;
    }
}

void Match::setHunterDrawTime(long timeMs) {
    hunterDrawTimeMs = (timeMs < MIN_DRAW_TIME_MS) ? MIN_DRAW_TIME_MS : timeMs;
}

void Match::setBountyDrawTime(long timeMs) {
    bountyDrawTimeMs = (timeMs < MIN_DRAW_TIME_MS) ? MIN_DRAW_TIME_MS : timeMs;
}

std::string Match::toJson() const {
    JsonDocument doc;
    JsonObject matchObj = doc.to<JsonObject>();
    matchObj[JSON_KEY_MATCH_ID] = match_id;
    matchObj[JSON_KEY_HUNTER_ID] = hunter;
    matchObj[JSON_KEY_BOUNTY_ID] = bounty;
    matchObj[JSON_KEY_WINNER_IS_HUNTER] = hunterDrawTimeMs < bountyDrawTimeMs;
    matchObj[JSON_KEY_HUNTER_TIME] = hunterDrawTimeMs;
    matchObj[JSON_KEY_BOUNTY_TIME] = bountyDrawTimeMs;

    std::string json;
    serializeJson(matchObj, json);
    return json;
}

void Match::fromJson(const std::string& json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (!error) {
        if (doc[JSON_KEY_MATCH_ID].is<const char*>()) {
            const char* v = doc[JSON_KEY_MATCH_ID].as<const char*>();
            IdGenerator::copyId(match_id, v);
            if (!UUIDBytes::parse(match_id, matchUuid)) {
                IdGenerator::uuidStringToBytes(match_id, matchUuid.bytes);
            }
        }
        if (doc[JSON_KEY_HUNTER_ID].is<const char*>()) {
            const char* v = doc[JSON_KEY_HUNTER_ID].as<const char*>();
            setHunterId(v);
        }
        if (doc[JSON_KEY_BOUNTY_ID].is<const char*>()) {
            const char* v = doc[JSON_KEY_BOUNTY_ID].as<const char*>();
            setBountyId(v);
        }
        if (doc[JSON_KEY_HUNTER_TIME].is<long>()) {
            setHunterDrawTime(doc[JSON_KEY_HUNTER_TIME].as<long>());
        }
        if (doc[JSON_KEY_BOUNTY_TIME].is<long>()) {
            setBountyDrawTime(doc[JSON_KEY_BOUNTY_TIME].as<long>());
        }
    }
}

size_t Match::serialize(uint8_t* buffer) const {
    size_t currentPos = 0;
    uint8_t playerIdBytes[PLAYER_ID_BINARY_SIZE];

    // Serialize matchUuid (16 raw bytes)
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

    // Serialize draw times
    memcpy(buffer + currentPos, &hunterDrawTimeMs, sizeof(long));
    currentPos += sizeof(long);
    memcpy(buffer + currentPos, &bountyDrawTimeMs, sizeof(long));
    currentPos += sizeof(long);

    // Serialize 16-byte UUIDs
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

    // Serialize MACs
    memcpy(buffer + currentPos, hunterMac, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;
    memcpy(buffer + currentPos, bountyMac, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;

    // Serialize occurredAtUnixMs
    memcpy(buffer + currentPos, &occurredAtUnixMs, sizeof(int64_t));
    currentPos += sizeof(int64_t);

    return currentPos;
}

size_t Match::deserialize(const uint8_t* buffer) {
    size_t currentPos = 0;

    // Deserialize matchUuid
    memcpy(matchUuid.bytes, buffer + currentPos, IdGenerator::UUID_BINARY_SIZE);
    matchUuid.format(match_id);
    currentPos += IdGenerator::UUID_BINARY_SIZE;

    // The wire field carries no terminator, so bound the read by the destination
    // the same way every other id copy does. copyId also clears the tail, which a
    // raw memcpy of the field would not.
    IdGenerator::copyId(hunter, reinterpret_cast<const char*>(buffer + currentPos));
    currentPos += PLAYER_ID_BINARY_SIZE;

    IdGenerator::copyId(bounty, reinterpret_cast<const char*>(buffer + currentPos));
    currentPos += PLAYER_ID_BINARY_SIZE;

    // Deserialize draw times
    long hunterTime = 0;
    long bountyTime = 0;
    memcpy(&hunterTime, buffer + currentPos, sizeof(long));
    currentPos += sizeof(long);
    memcpy(&bountyTime, buffer + currentPos, sizeof(long));
    currentPos += sizeof(long);
    setHunterDrawTime(hunterTime);
    setBountyDrawTime(bountyTime);

    // Deserialize 16-byte UUIDs
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

    // Deserialize MACs
    memcpy(hunterMac, buffer + currentPos, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;
    memcpy(bountyMac, buffer + currentPos, MAC_BINARY_SIZE);
    currentPos += MAC_BINARY_SIZE;

    // Deserialize occurredAtUnixMs
    memcpy(&occurredAtUnixMs, buffer + currentPos, sizeof(int64_t));
    currentPos += sizeof(int64_t);

    return currentPos;
}
