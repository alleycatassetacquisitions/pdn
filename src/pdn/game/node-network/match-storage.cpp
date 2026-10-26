#include "game/node-network/match-storage.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace {
const char* const STORAGE_NAMESPACE = "evtmatches";
const char* const COUNT_KEY = "count";
constexpr size_t ENCODED_RECORD_SIZE = 6 * UUIDBytes::SIZE + 3 * sizeof(uint64_t);

std::string recordKey(size_t index) {
    return "r" + std::to_string(index);
}

void appendUuid(std::string& out, const UUIDBytes& uuid) {
    out.append(reinterpret_cast<const char*>(uuid.bytes), UUIDBytes::SIZE);
}

void appendUint64(std::string& out, uint64_t value) {
    for (size_t shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void readUuid(const std::string& in, size_t& offset, UUIDBytes& uuid) {
    memcpy(uuid.bytes, in.data() + offset, UUIDBytes::SIZE);
    offset += UUIDBytes::SIZE;
}

uint64_t readUint64(const std::string& in, size_t& offset) {
    uint64_t value = 0;
    for (size_t shift = 0; shift < 64; shift += 8) {
        value |= static_cast<uint64_t>(static_cast<uint8_t>(in[offset++])) << shift;
    }
    return value;
}

std::string encodeRecord(const QuickdrawMatchRecord& record) {
    std::string out;
    out.reserve(ENCODED_RECORD_SIZE);
    appendUuid(out, record.matchId);
    appendUuid(out, record.playerAId);
    appendUuid(out, record.playerBId);
    appendUuid(out, record.playerATeamId);
    appendUuid(out, record.playerBTeamId);
    appendUuid(out, record.winnerPlayerId);
    appendUint64(out, record.playerADrawTimeMs);
    appendUint64(out, record.playerBDrawTimeMs);
    appendUint64(out, static_cast<uint64_t>(record.occurredAtUnixMs));
    return out;
}
}  // namespace

MatchStorage::MatchStorage(StorageInterface* storage)
    : storage(storage) {
}

size_t MatchStorage::getCount() const {
    return storage->readUChar(STORAGE_NAMESPACE, COUNT_KEY, 0);
}

bool MatchStorage::append(const QuickdrawMatchRecord& record) {
    const size_t count = getCount();
    if (count >= MAX_PENDING_RECORDS) {
        return false;
    }

    const std::string encoded = encodeRecord(record);
    if (storage->write(STORAGE_NAMESPACE, recordKey(count), encoded) != encoded.size()) {
        return false;
    }
    return storage->writeUChar(STORAGE_NAMESPACE, COUNT_KEY, static_cast<uint8_t>(count + 1)) == 1;
}

bool MatchStorage::readAt(size_t index, QuickdrawMatchRecord& record) const {
    if (index >= getCount()) {
        return false;
    }

    const std::string encoded = storage->read(STORAGE_NAMESPACE, recordKey(index), "");
    if (encoded.size() != ENCODED_RECORD_SIZE) {
        return false;
    }

    QuickdrawMatchRecord loaded;
    size_t offset = 0;
    readUuid(encoded, offset, loaded.matchId);
    readUuid(encoded, offset, loaded.playerAId);
    readUuid(encoded, offset, loaded.playerBId);
    readUuid(encoded, offset, loaded.playerATeamId);
    readUuid(encoded, offset, loaded.playerBTeamId);
    readUuid(encoded, offset, loaded.winnerPlayerId);
    loaded.playerADrawTimeMs = readUint64(encoded, offset);
    loaded.playerBDrawTimeMs = readUint64(encoded, offset);
    loaded.occurredAtUnixMs = static_cast<int64_t>(readUint64(encoded, offset));
    record = loaded;
    return true;
}

void MatchStorage::clear() {
    storage->clear(STORAGE_NAMESPACE);
}
