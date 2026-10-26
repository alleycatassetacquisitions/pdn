#pragma once

#include <cstddef>
#include <cstdint>

#include "device/drivers/platform-clock.hpp"
#include "device/drivers/storage-interface.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/node-network/duel-session.hpp"
#include "game/node-network/match-storage.hpp"
#include "game/player-provider.hpp"
#include "wireless/node-network-wireless-manager.hpp"

/** Draw times this close (inclusive) count as a synchronized duel. */
constexpr uint64_t NODE_SYNC_WINDOW_MS = 250;

/** Failed duels in a row with one peer before the device should give up and sleep. */
constexpr uint32_t MAX_FAILED_ATTEMPTS = 3;

/** An offer timestamp this many times smaller than the responder's clock is treated as unset. */
constexpr int64_t TIME_IMPLAUSIBLE_RATIO = 1000;

/**
 * Runs event duels over the cable: handshake, draw exchange, sync decision, and persisting the
 * finished record. States measure reaction time and report it here.
 */
class MatchManagerV2 {
public:
    MatchManagerV2(PlayerProvider* playerProvider, StorageInterface* storageInterface,
                   NodeNetworkWirelessManager* nodeNetworkWirelessManager,
                   RemoteDeviceCoordinator* remoteDeviceCoordinator, PlatformClock* platformClock);
    ~MatchManagerV2();

    MatchManagerV2(const MatchManagerV2&) = delete;
    MatchManagerV2& operator=(const MatchManagerV2&) = delete;

    /** Sets which end of the cable this device is. */
    void setLocalLinkSide(DuelLinkSide side);

    /**
     * Opens a duel by sending an OFFER to @p opponentMac.
     *
     * @return false when not the initiator, a duel is active, the player is unregistered, or the
     *         unix clock is not positive.
     */
    bool startMatch(const uint8_t* opponentMac);

    /** Handles one validated packet. Packets that fail any gate are ignored. */
    void onPacket(const uint8_t* senderMac, const NodeNetworkPacket& packet);

    /** Records and sends the local reaction time. @return false when it cannot be recorded. */
    bool onLocalDrawTimeMs(uint64_t drawTimeMs);

    /** Declares that the local player never pressed within @p elapsedMs. */
    bool onLocalGraceExpired(uint64_t elapsedMs);

    bool isHandshakeComplete() const;
    bool isExchangeComplete() const;

    /** @return true when both draws are in, none was missed, and they fall within the sync window. */
    bool didSync() const;

    /** Stores the record for a synced duel and clears the session. @return false otherwise. */
    bool finalizeMatch();

    /** Counts the outcome against the failed-attempt limit for the current peer. */
    void recordLinkOutcome(bool success);

    uint32_t getFailedAttempts() const;
    bool hasExhaustedAttempts() const;

    size_t getPendingRecordCount() const;
    bool readPendingRecord(size_t index, QuickdrawMatchRecord& record) const;
    void clearPendingRecords();

    /** Drops the active session; the failed-attempt count is kept. */
    void clearCurrentMatch();

    const DuelSession& getSession() const;

private:
    bool buildLocalParticipant(DuelParticipant& participant) const;
    void trackPeer(const uint8_t* peerMac);
    void sendOffer();
    void sendToOpponent(NodeNetworkCommand command, uint64_t drawTimeMs);
    void sendReply(const uint8_t* destinationMac, NodeNetworkCommand command, const UUIDBytes& matchId,
                   int64_t unixMs) const;
    bool isOpponentSideSender(const NodeNetworkPacket& packet) const;

    void onOffer(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onAccept(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onTimeCorrection(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onDraw(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onNeverPressed(const uint8_t* senderMac, const NodeNetworkPacket& packet);

    PlayerProvider* playerProvider = nullptr;
    NodeNetworkWirelessManager* nodeNetworkWirelessManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    PlatformClock* platformClock = nullptr;
    MatchStorage* matchStorage = nullptr;
    DuelSession session;
    DuelLinkSide localLinkSide = DuelLinkSide::INITIATOR;
    bool timeCorrectionApplied = false;
    bool hasTrackedPeer = false;
    uint8_t trackedPeerMac[6] = {};
    uint32_t failedAttempts = 0;
};
