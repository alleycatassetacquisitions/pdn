#include "apps/hacking/hacked-players-manager.hpp"
#include "fdn-constants.hpp"

HackedPlayersManager::HackedPlayersManager(StorageInterface* storage)
    : storage(storage) {}

HackedPlayersManager::~HackedPlayersManager() {
    storage = nullptr;
}

void HackedPlayersManager::playerHackSuccessful(const std::string& playerId) {
    storage->writeUChar(FDN_PREF_NAMESPACE, playerId, HACK_STATUS_LOCAL);
}

// A status of 2 written by older firmware (uploaded) still reads as hacked.
bool HackedPlayersManager::hasPlayerHacked(const std::string& playerId) const {
    return storage->readUChar(FDN_PREF_NAMESPACE, playerId, HACK_STATUS_NONE) >= HACK_STATUS_LOCAL;
}
