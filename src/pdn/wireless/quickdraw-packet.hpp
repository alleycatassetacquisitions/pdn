#pragma once

#include <cstdint>
#include <cstring>

#include "id-generator.hpp"

// Wire format transmitted over ESP-NOW for every quickdraw command.
// Defined here so tests can construct and inspect packets without duplicating the layout.
struct QuickdrawPacket {
    char matchId[37];  // IdGenerator::UUID_BUFFER_SIZE
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;
    int command;
    // Reliable-channel sequence number; stamped by sendReliable, read back by the
    // receiver's dedup. Zero means the frame was never sent through a channel.
    uint8_t seqId;
} __attribute__((packed));

/// Wire values for QuickdrawPacket::command.
namespace QDCommand {
constexpr int SEND_MATCH_ID = 6;
constexpr int MATCH_ID_ACK = 7;
constexpr int MATCH_ROLE_MISMATCH = 8;
constexpr int DRAW_RESULT = 9;
constexpr int NEVER_PRESSED = 10;
constexpr int INVALID_COMMAND = 0xFF;
}  // namespace QDCommand

struct QuickdrawCommand {
    const uint8_t* wifiMacAddr;
    int command;
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;

    /// Decoded form of one duel frame. On the receive path `macAddress` is the
    /// sender, which is what every gate here matches against; an outbound command
    /// fills it with the destination and sendCommand ignores it.
    QuickdrawCommand(const uint8_t* macAddress, int command, const char* matchId, const char* playerId, long playerDrawTime, bool isHunter)
        : wifiMacAddr(macAddress)
        , command(command)
        , playerDrawTime(playerDrawTime)
        , isHunter(isHunter) {

        // Bounded by the source, not the destination: a caller handing over a
        // shorter string than the field would otherwise be read past the end of.
        IdGenerator::copyId(this->matchId, matchId);
        IdGenerator::copyId(this->playerId, playerId);
    }
};
