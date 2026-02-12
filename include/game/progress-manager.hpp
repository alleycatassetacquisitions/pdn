#pragma once

#include "game/player.hpp"
#include "device/drivers/storage-interface.hpp"
#include <cstdint>

/*
 * ProgressManager persists Konami progress to NVS storage.
 *
 * NVS keys:
 *   "konami" — uint8_t bitmask of unlocked Konami buttons
 *   "synced" — uint8_t (0 or 1), whether progress has been uploaded
 */
class ProgressManager {
public:
    ProgressManager() = default;
    ~ProgressManager() = default;

    void initialize(Player* player, StorageInterface* storage) {
        this->player = player;
        this->storage = storage;
    }

    void saveProgress() {
        if (!storage || !player) return;
        storage->writeUChar("konami", player->getKonamiProgress());
        storage->writeUChar("synced", 0);
        synced = false;
    }

    void loadProgress() {
        if (!storage || !player) return;
        uint8_t progress = storage->readUChar("konami", 0);
        if (progress > 0) {
            player->setKonamiProgress(progress);
        }
    }

    bool hasUnsyncedProgress() const { return !synced; }

    void clearProgress() {
        if (!storage) return;
        storage->writeUChar("konami", 0);
        storage->writeUChar("synced", 1);
        synced = true;
    }

private:
    Player* player = nullptr;
    StorageInterface* storage = nullptr;
    bool synced = true;
};
