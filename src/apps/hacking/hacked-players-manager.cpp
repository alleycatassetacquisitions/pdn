#include "apps/hacking/hacked-players-manager.hpp"
#include <algorithm>
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
    return pendingCache;
}

void HackedPlayersManager::addToPending(const std::string& playerId) {
    pendingCache.push_back(playerId);
}

void HackedPlayersManager::removeFromPending(const std::string& playerId) {
    pendingCache.erase(
        std::remove(pendingCache.begin(), pendingCache.end(), playerId),
        pendingCache.end());
}
