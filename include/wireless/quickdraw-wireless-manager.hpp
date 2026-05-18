#pragma once

//
// Created by Elli Furedy on 1/21/2025.
//
#include <vector>
#include <cstring>  // For memcpy
#include <functional>
#include <map>
#include <cstdint>
#include "utils/simple-timer.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "id-generator.hpp"
#include "mac-functions.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/resender.hpp"

// Wire format transmitted over ESP-NOW for every quickdraw command.
// Defined here so tests can construct and inspect packets without duplicating the layout.
// seqId is used for Resender's ack matching; 0 means "fire-and-forget, no ack expected".
struct QuickdrawPacket {
    char matchId[37];  // IdGenerator::UUID_BUFFER_SIZE
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;
    int  command;
    uint8_t seqId;
} __attribute__((packed));

enum QDCommand {
    // Game Commands
    // HACK = 6,
    // HACK_ACK = 7, 
    // HACK_CONFIRMED = 8,
    // LOCKDOWN = 9,
    // LOCKDOWN_ACK = 10,
    // LOCKDOWN_CONFIRMED = 11,    
    SEND_MATCH_ID = 6,
    MATCH_ID_ACK = 7,
    MATCH_ROLE_MISMATCH = 8,
    DRAW_RESULT = 9,
    NEVER_PRESSED = 10,
    COMMAND_COUNT,  // Always add new commands above this line
    INVALID_COMMAND = 0xFF
};

struct QuickdrawCommand {
    const uint8_t* wifiMacAddr;
    int command;
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;
    uint8_t seqId;

    QuickdrawCommand(const uint8_t* macAddress, int command, const char* matchId, const char* playerId, long playerDrawTime, bool isHunter, uint8_t seqId = 0)
        : command(command), playerDrawTime(playerDrawTime), isHunter(isHunter), seqId(seqId) {
        this->wifiMacAddr = macAddress;

        memcpy(this->matchId, matchId, IdGenerator::UUID_BUFFER_SIZE);

        memcpy(this->playerId, playerId, 5);
    }
};

using QDCommandTracker = std::map<int, QuickdrawCommand>;

class QuickdrawWirelessManager {
public:
    QuickdrawWirelessManager();
    virtual ~QuickdrawWirelessManager();

    void initialize(Player* player, WirelessManager* wirelessManager, long broadcastCooldown);

    int processQuickdrawCommand(const uint8_t* macAddress, const uint8_t* data, const size_t dataLen);

    void setPacketReceivedCallback(const std::function<void(const QuickdrawCommand&)>& callback);

    // Fire-and-forget send for commands with their own recovery
    // (matchInitializationTimer in Idle handles SEND_MATCH_ID etc.).
    virtual int broadcastPacket(const uint8_t* macAddress,
                               QuickdrawCommand& command);

    // Reliable send via Resender; assigns seqId. Used for DRAW_RESULT /
    // NEVER_PRESSED, where packet loss would corrupt real match results.
    virtual int broadcastReliable(const uint8_t* macAddress,
                                  QuickdrawCommand& command);

    using AbandonCallback = std::function<void(int command, const uint8_t* targetMac)>;
    void setAbandonCallback(AbandonCallback cb);

    // True iff a reliable send for (command, target) is still in flight.
    bool isPendingReliable(const uint8_t* target, int command) const {
        if (resender_ == nullptr) return false;
        return resender_->isPending(PktType::kQuickdrawCommand,
                                    static_cast<uint8_t>(command), target);
    }

    // Drop any in-flight DRAW_RESULT / NEVER_PRESSED to the given peer.
    // Without this, a stale abandon arriving after the match clears could
    // void the next match primed against the same peer.
    void cancelPendingReliable(const uint8_t* target) {
        if (resender_ == nullptr || target == nullptr) return;
        resender_->cancel(PktType::kQuickdrawCommand, QDCommand::DRAW_RESULT, target);
        resender_->cancel(PktType::kQuickdrawCommand, QDCommand::NEVER_PRESSED, target);
    }

    // Must be called every loop tick to drive Resender retransmits.
    void sync();

    void clearCallbacks();

private:
    WirelessManager* wirelessManager;

    std::function<void(const QuickdrawCommand&)> packetReceivedCallback;
    AbandonCallback abandonCallback_;

    Player* player;

    QDCommandTracker commandTracker;

    SimpleTimer broadcastTimer;

    long broadcastDelay;

    Resender* resender_ = nullptr;
    uint8_t nextSeqId_ = 1;  // skip 0 (= fire-and-forget sentinel)

    // Last-seen seqId per (sender mac, command) so retries of the same logical
    // packet are recognised and acked but not re-applied. Grows by one entry
    // per unique (mac, command) ever observed; bounded in practice by the
    // ESP-NOW peer table cap (20) × reliable QDCommand count (2) ≈ 40, so no
    // pruning is needed.
    struct ObservedKey {
        std::array<uint8_t, 6> mac;
        uint8_t command;
        uint8_t lastSeqId;
    };
    std::vector<ObservedKey> observed_;
    bool isDuplicate(const uint8_t* mac, uint8_t command, uint8_t seqId);
    void sendAck(const uint8_t* toMac, uint8_t command, uint8_t seqId);
};
