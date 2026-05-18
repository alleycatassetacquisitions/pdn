#pragma once

#include <array>
#include <optional>
#include <vector>
#include <cstdint>
#include <cstring>
#include "game/player.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/peer-comms-types.hpp"
#include "wireless/resender.hpp"
#include "device/drivers/serial-wrapper.hpp"

enum class ChainGameEventType : uint8_t {
    COUNTDOWN = 0,
    DRAW = 1,
    WIN = 2,
    LOSS = 3,
};

// seqId = 0 is a sentinel meaning "no ACK expected, no retry". Used for
// COUNTDOWN and DRAW which are time-critical and must not arrive late.
// WIN and LOSS use a nonzero seqId and are retransmitted until ACKed or
// the retry budget is exhausted.
struct ChainGameEventPayload {
    uint8_t event_type;
    uint8_t seqId;
} __attribute__((packed));

class ChainDuelManager {
public:
    ChainDuelManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc);
    virtual ~ChainDuelManager() { delete resender_; }

    bool isChampion() const;
    bool isSupporter() const;
    virtual bool isLoop() const;
    bool canInitiateMatch() const;
    std::vector<std::array<uint8_t, 6>> getSupporterChainPeers() const;

    void sendGameEventToSupporters(ChainGameEventType eventType);
    void sendConfirm();

    // Supporter-side: ACK a received WIN/LOSS game event back to the
    // champion so it stops retransmitting. Called from Quickdraw's packet
    // handler when the incoming payload has seqId != 0.
    void sendGameEventAck(const uint8_t* toMac, uint8_t seqId);

    bool isKnownGameEventSender(const uint8_t* fromMac) const;
    void onConfirmReceived(
        const uint8_t* fromMac,
        const uint8_t* originatorMac,
        uint8_t seqId);
    void onChainStateChanged();

    // Records a peer's role learned from an incoming kRoleAnnounce packet.
    void setPeerRole(SerialIdentifier port, bool isHunter);

    unsigned long getBoostMs() const;
    size_t getConfirmedSupporterCount() const;
    void clearSupporterConfirms();

    const uint8_t* getChampionMac() const;
    void onRoleAnnounceReceived(
        const uint8_t* fromMac,
        uint8_t role,
        const uint8_t* championMac,
        uint8_t seqId);
    void broadcastRoleAndChampion();
    void sendRoleToOpponentJack();
    void sync();

    static constexpr unsigned long BOOST_PER_SUPPORTER_MS = 15;

    using RetryStats = Resender::Stats;
    RetryStats getRetryStats() const {
        return resender_ ? resender_->getStats() : RetryStats{};
    }
    RetryStats getRetryStats(PktType type) const {
        return resender_ ? resender_->getStats(type) : RetryStats{};
    }

private:
    Resender* resender_ = nullptr;
    Player* player_;
    WirelessManager* wirelessManager_;
    RemoteDeviceCoordinator* rdc_;

    SerialIdentifier opponentJack() const;
    SerialIdentifier supporterJack() const;

    // Returns the cached role of the direct peer on `port`, or nullopt if no
    // role announcement has been received from the current direct peer.
    std::optional<bool> peerIsHunter(SerialIdentifier port) const;

    std::vector<std::array<uint8_t, 6>> confirmedSupporters_;
    unsigned long boostMs_ = 0;
    size_t lastSupporterChainCount_ = 0;

    uint8_t nextConfirmSeqId_ = 1;  // skip 0 as sentinel

    // Per-port direct peer role; cleared when the direct peer disconnects.
    std::array<std::optional<bool>, 2> peerRoleByPort_;

    std::optional<std::array<uint8_t, 6>> championMac_;
    std::optional<std::array<uint8_t, 6>> lastAnnouncedSupporterJackMac_;
    std::optional<std::array<uint8_t, 6>> lastAnnouncedOpponentJackMac_;

    uint8_t nextRoleAnnounceSeqId_ = 1;
    uint8_t nextGameEventSeqId_ = 1;
};
