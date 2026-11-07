#pragma once

#include <cstdint>

#include "id-generator.hpp"

struct DuelPacket {
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];
    uint8_t isHunter;
    long playerDrawTime;
    int command;
    uint8_t seqId;
} __attribute__((packed));

namespace DuelCmd {
constexpr int SEND_MATCH_ID = 6;
constexpr int MATCH_ID_ACK = 7;
constexpr int MATCH_ROLE_MISMATCH = 8;
constexpr int DRAW_RESULT = 9;
constexpr int NEVER_PRESSED = 10;
}

struct DuelCommand {
    const uint8_t* wifiMacAddr;
    int command;
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];
    bool isHunter;
    long playerDrawTime;

    /** Decoded duel frame received from macAddress. */
    DuelCommand(const uint8_t* macAddress, int command, const char* matchId, const char* playerId, long playerDrawTime, bool isHunter)
        : wifiMacAddr(macAddress)
        , command(command)
        , isHunter(isHunter)
        , playerDrawTime(playerDrawTime) {
        IdGenerator::copyId(this->matchId, matchId);
        IdGenerator::copyId(this->playerId, playerId);
    }
};
