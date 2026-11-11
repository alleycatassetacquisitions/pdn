#pragma once

#include <cstdint>

#include "id-generator.hpp"
#include "utils/uuid-bytes.hpp"

struct MatchPacket {
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];
    UUIDBytes playerUuid;
    UUIDBytes teamUuid;
    uint8_t isHunter;
    long playerDrawTime;
    int command;
    uint8_t seqId;
} __attribute__((packed));

namespace MatchCmd {
constexpr int SEND_MATCH_ID = 6;
constexpr int MATCH_ID_ACK = 7;
constexpr int MATCH_ROLE_MISMATCH = 8;
constexpr int DRAW_RESULT = 9;
constexpr int NEVER_PRESSED = 10;
}

struct MatchCommand {
    const uint8_t* wifiMacAddr;
    int command;
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];
    UUIDBytes playerUuid;
    UUIDBytes teamUuid;
    bool isHunter;
    long playerDrawTime;

    /** Decoded match frame received from macAddress. */
    MatchCommand(const uint8_t* macAddress, int command, const char* matchId, const char* playerId,
                 long playerDrawTime, bool isHunter, const UUIDBytes& playerUuid = UUIDBytes{},
                 const UUIDBytes& teamUuid = UUIDBytes{})
        : wifiMacAddr(macAddress)
        , command(command)
        , playerUuid(playerUuid)
        , teamUuid(teamUuid)
        , isHunter(isHunter)
        , playerDrawTime(playerDrawTime) {
        IdGenerator::copyId(this->matchId, matchId);
        IdGenerator::copyId(this->playerId, playerId);
    }
};
