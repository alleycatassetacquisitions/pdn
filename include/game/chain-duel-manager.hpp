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
#include "device/drivers/serial-wrapper.hpp"

enum class ChainGameEventType : uint8_t {
    COUNTDOWN = 0,
    DRAW = 1,
    WIN = 2,
    LOSS = 3,
};

struct ChainGameEventPayload {
    uint8_t event_type;
} __attribute__((packed));

class ChainDuelManager {
public:
    ChainDuelManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc);
    ~ChainDuelManager() = default;

    bool isChampion() const;
    bool isSupporter() const;
    bool isLoop() const;
    bool canInitiateMatch() const;
    std::vector<std::array<uint8_t, 6>> getSupporterChainPeers() const;

    void broadcastGameEvent(ChainGameEventType eventType);
    void sendConfirm();

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
    void onRoleAnnounceAckReceived(const uint8_t* fromMac, uint8_t seqId);
    void broadcastRoleAndChampion();
    void sendRoleToOpponentJack();
    void sync();

    static constexpr unsigned long BOOST_PER_SUPPORTER_MS = 15;

    // Retry observability for the role-announce channel. Mirrors
    // RemoteDeviceCoordinator::RetryStats semantics.
    struct RetryStats {
        uint32_t sends = 0;
        uint32_t retries = 0;
        uint32_t abandons = 0;
        uint32_t ackLatencyMsSum = 0;
        uint32_t ackCount = 0;
    };
    RetryStats getRetryStats() const { return retryStats_; }

private:
    RetryStats retryStats_;
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

    struct PendingRoleAnnounce {
        bool active = false;
        uint8_t seqId = 0;
        uint8_t retries = 0;
        std::array<uint8_t, 6> championMac;
        uint8_t role;
        std::array<uint8_t, 6> targetMac;
        SimpleTimer timer;
    };
    PendingRoleAnnounce pending_;
    static constexpr unsigned long kAckTimeoutMs = 100;
    static constexpr uint8_t kMaxRetries = 3;

    uint8_t nextRoleAnnounceSeqId_ = 1;
};
