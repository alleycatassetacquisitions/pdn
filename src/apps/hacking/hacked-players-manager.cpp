#include "apps/hacking/hacked-players-manager.hpp"
#include <sstream>

HackedPlayersManager::HackedPlayersManager(StorageInterface* storage)
    : storage(storage) {}

HackedPlayersManager::~HackedPlayersManager() {
    storage = nullptr;
}

void HackedPlayersManager::playerHackSuccessful(const std::string& playerId) {
    storage->writeUChar(playerId, HACK_STATUS_LOCAL);
    addToPending(playerId);
}

void HackedPlayersManager::playerHackUploaded(const std::string& playerId) {
    storage->writeUChar(playerId, HACK_STATUS_UPLOADED);
    removeFromPending(playerId);
}

bool HackedPlayersManager::hasPlayerHacked(const std::string& playerId) const {
    return storage->readUChar(playerId, HACK_STATUS_NONE) >= HACK_STATUS_LOCAL;
}

std::vector<std::string> HackedPlayersManager::getPendingUploads() const {
    std::string raw = storage->read(PENDING_KEY, "");
    std::vector<std::string> result;
    if (raw.empty()) return result;

    std::istringstream stream(raw);
    std::string id;
    while (std::getline(stream, id, ',')) {
        if (!id.empty()) result.push_back(id);
    }
    return result;
}

void HackedPlayersManager::addToPending(const std::string& playerId) {
    std::string current = storage->read(PENDING_KEY, "");
    if (!current.empty()) current += ',';
    current += playerId;
    storage->write(PENDING_KEY, current);
}

void HackedPlayersManager::removeFromPending(const std::string& playerId) {
    std::vector<std::string> pending = getPendingUploads();
    std::string updated;
    for (const auto& id : pending) {
        if (id == playerId) continue;
        if (!updated.empty()) updated += ',';
        updated += id;
    }
    storage->write(PENDING_KEY, updated);
}
