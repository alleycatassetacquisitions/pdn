#pragma once

#include <string>
#include <vector>
#include "device/drivers/storage-interface.hpp"

// Storage values per player ID key:
//   0 = not hacked
//   1 = hacked locally, upload pending
//   2 = hacked and successfully uploaded
static constexpr uint8_t HACK_STATUS_NONE     = 0;
static constexpr uint8_t HACK_STATUS_LOCAL    = 1;
static constexpr uint8_t HACK_STATUS_UPLOADED = 2;

class HackedPlayersManager {
public:
    explicit HackedPlayersManager(StorageInterface* storage);
    ~HackedPlayersManager();

    void playerHackSuccessful(const std::string& playerId);
    void playerHackUploaded(const std::string& playerId);

    bool hasPlayerHacked(const std::string& playerId) const;
    std::vector<std::string> getPendingUploads() const;

private:

    StorageInterface* storage;
    mutable std::vector<std::string> pendingCache;

    void addToPending(const std::string& playerId);
    void removeFromPending(const std::string& playerId);
};
