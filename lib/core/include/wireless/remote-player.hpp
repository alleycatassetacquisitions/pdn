#pragma once

#include "game/player.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

struct RemotePlayer
{
    uint8_t wifiMacAddr[6];
    Player playerInfo;
    unsigned long lastSeenTime;
    signed rssi;

    RemotePlayer(const uint8_t* macAddr, std::string id, Allegiance allegiance, bool isHunter,
                 unsigned long lastSeen, signed rssiDb) :
                 playerInfo(id, allegiance, isHunter),
                 lastSeenTime(lastSeen),
                 rssi(rssiDb)
    {
        memcpy(wifiMacAddr, macAddr, 6);
    }
};