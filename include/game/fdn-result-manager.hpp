#pragma once

#include "device/drivers/storage-interface.hpp"
#include "device/device-types.hpp"
#include "device/device-constants.hpp"
#include <string>

/*
 * FdnResultManager caches NPC game results in NVS for deferred upload.
 *
 * Storage format (NVS key-value):
 *   "npc_count" -> uint8_t (number of cached results)
 *   "npc_res_0" -> string "gameType:won:score:difficulty" (e.g. "7:1:850:1")
 *   "npc_res_1" -> ...
 */
class FdnResultManager {
public:
    FdnResultManager() = default;
    ~FdnResultManager() = default;

    void initialize(StorageInterface* storage) {
        this->storage = storage;
    }

    void cacheResult(GameType gameType, bool won, int score, bool hardMode) {
        if (!storage) return;

        uint8_t count = storage->readUChar(NPC_RESULT_COUNT_KEY, 0);
        if (count >= MAX_NPC_RESULTS) return;

        std::string key = std::string(NPC_RESULT_KEY) + std::to_string(count);
        std::string value = std::to_string(static_cast<int>(gameType)) + ":" +
                            (won ? "1" : "0") + ":" +
                            std::to_string(score) + ":" +
                            (hardMode ? "1" : "0");

        storage->write(key, value);
        storage->writeUChar(NPC_RESULT_COUNT_KEY, count + 1);
    }

    uint8_t getCachedResultCount() {
        if (!storage) return 0;
        return storage->readUChar(NPC_RESULT_COUNT_KEY, 0);
    }

    std::string getCachedResult(uint8_t index) {
        if (!storage) return "";
        std::string key = std::string(NPC_RESULT_KEY) + std::to_string(index);
        return storage->read(key, "");
    }

    void clearCachedResults() {
        if (!storage) return;
        uint8_t count = storage->readUChar(NPC_RESULT_COUNT_KEY, 0);
        for (uint8_t i = 0; i < count; i++) {
            std::string key = std::string(NPC_RESULT_KEY) + std::to_string(i);
            storage->remove(key);
        }
        storage->writeUChar(NPC_RESULT_COUNT_KEY, 0);
    }

private:
    StorageInterface* storage = nullptr;
};
