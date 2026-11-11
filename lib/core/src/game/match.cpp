#include "game/match.hpp"
#include "id-generator.hpp"
#include "device/drivers/logger.hpp"
#include <cstring>

Match::Match(const char* mid) {
    IdGenerator::copyId(matchId, mid);
    // Shootout ids are not UUID-shaped; this packs their hex digits the same way.
    IdGenerator::uuidStringToBytes(matchId, matchUuid.bytes);
}
size_t Match::serialize(uint8_t* buffer) const {
    MatchRecord record = {};
    record.matchUuid = matchUuid;
    record.hunterDrawTimeMs = static_cast<uint32_t>(hunterDrawTimeMs);
    record.bountyDrawTimeMs = static_cast<uint32_t>(bountyDrawTimeMs);
    record.hunterPlayerId = hunterPlayerId;
    record.bountyPlayerId = bountyPlayerId;
    record.hunterTeamId = hunterTeamId;
    record.bountyTeamId = bountyTeamId;
    record.winnerPlayerId = winnerPlayerId;
    record.occurredAtUnixMs = occurredAtUnixMs;
    memcpy(buffer, &record, sizeof(record));
    return sizeof(record);
}

size_t Match::deserialize(const uint8_t* buffer) {
    MatchRecord record;
    memcpy(&record, buffer, sizeof(record));
    matchUuid = record.matchUuid;
    matchUuid.format(matchId);
    hunterDrawTimeMs = record.hunterDrawTimeMs;
    bountyDrawTimeMs = record.bountyDrawTimeMs;
    hunterPlayerId = record.hunterPlayerId;
    bountyPlayerId = record.bountyPlayerId;
    hunterTeamId = record.hunterTeamId;
    bountyTeamId = record.bountyTeamId;
    winnerPlayerId = record.winnerPlayerId;
    occurredAtUnixMs = record.occurredAtUnixMs;
    return sizeof(record);
}
