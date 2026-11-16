#pragma once

#include <cstdint>
#include <string>
#include "device/drivers/storage-interface.hpp"

static constexpr uint8_t HACK_STATUS_NONE     = 0;
static constexpr uint8_t HACK_STATUS_LOCAL = 1;

class HackedPlayersManager {
public:
    explicit HackedPlayersManager(StorageInterface* storage);
    ~HackedPlayersManager();

    void playerHackSuccessful(const std::string& playerId);

    bool hasPlayerHacked(const std::string& playerId) const;

private:
    StorageInterface* storage;
};
