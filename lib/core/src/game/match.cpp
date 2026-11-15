#include "game/match.hpp"
#include "id-generator.hpp"
#include <cstring>

Match::Match(const char* mid) {
    IdGenerator::copyId(matchId, mid);
    // Shootout ids are not UUID-shaped; their bytes are never stored.
    IdGenerator::uuidStringToBytes(matchId, record.matchUuid.bytes);
}

size_t Match::serialize(uint8_t* buffer) const {
    memcpy(buffer, &record, sizeof(record));
    return sizeof(record);
}

size_t Match::deserialize(const uint8_t* buffer) {
    memcpy(&record, buffer, sizeof(record));
    record.matchUuid.format(matchId);
    return sizeof(record);
}
