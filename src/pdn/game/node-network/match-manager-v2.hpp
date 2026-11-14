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

/** Failed rounds in a row with one peer before the device should give up and sleep. */
constexpr uint32_t MAX_FAILED_ATTEMPTS = 3;

/** An offer timestamp this many times smaller than the responder's clock is treated as unset. */
constexpr int64_t TIME_IMPLAUSIBLE_RATIO = 1000;

/** Slack on top of NODE_COUNTDOWN_MS before a SEQUENCE's step 0 counts as implausibly far out. */
constexpr int32_t NODE_SEQUENCE_LEAD_SLACK_MS = 1000;

// A responder that gives up while OUTCOME is still being resent could ACK a late copy after
// aborting, leaving the initiator to count a round the responder dropped.
static_assert(NODE_RESULT_GRACE_MS + NODE_RELIABLE_RETRY_MS * NODE_RELIABLE_MAX_ATTEMPTS <
              NODE_OUTCOME_WAIT_MS,
              "responder must outwait every OUTCOME resend");
static_assert(NODE_NETWORK_PACKET_STEPS == NODE_SEQUENCE_LENGTH,
              "SEQUENCE must carry every step's prompts");

/**
 * Runs event rounds over the cable: handshake, clock sync, the shared input schedule with the
 * initiator as coordinator, the outcome, aborts, and persisting the finished record. States own
 * all timing and report each step's result here.
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

    /** Marks the cable side unknown; incoming OFFERs are ignored and startMatch refuses. */
    void clearLocalLinkSide();

    /**
     * Opens a duel by sending an OFFER to @p opponentMac.
     *
     * @return false when the link side is unset or not the initiator, a duel is active, the
     *         player is unregistered, or the unix clock is not positive.
     */
    bool startMatch(const uint8_t* opponentMac);

    /** Handles one validated packet. Packets that fail any gate are ignored. */
    void onPacket(const uint8_t* senderMac, const NodeNetworkPacket& packet);

    /** Handles the wireless layer's verdict on one reliable packet. */
    void onDelivery(const uint8_t* destinationMac, const NodeNetworkPacket& packet, bool delivered);

    /** Initiator: sends one SYNC probe stamped with the local uptime. */
    void sendSyncProbe();

    /** @return true when a sync sample's round trip is at most NODE_SYNC_MAX_RTT_MS. */
    bool hasClockSync() const;

    /**
     * Initiator: stores @p sequence with step 0 NODE_COUNTDOWN_MS from now and sends it reliably
     * as SEQUENCE, with step 0 converted to the responder's uptime.
     *
     * @return false without a handshake, a good sync, or when a sequence is already set.
     */
    bool beginRound(const NodeInputSequence& sequence);

    /** @return true once this device has the round's sequence. */
    bool hasSequence() const;

    /**
     * Milliseconds until @p step starts on this device's clock; negative once it has started.
     * Only meaningful once a sequence is set.
     */
    int32_t msUntilStep(uint8_t step) const;

    /** Milliseconds until @p step's input window closes on this device's clock. */
    int32_t msUntilWindowCloses(uint8_t step) const;

    /**
     * Records the local player's judgment of their part of @p step and sends it reliably as
     * INPUT_RESULT with the @p pressCount buttons pressed.
     *
     * @return false when the local player does not press the step or already reported it.
     */
    bool onLocalInputResult(uint8_t step, bool success, uint8_t pressCount);

    /** Tells the opponent, unreliably, that @p pressCount buttons of @p step are pressed. */
    void onLocalPress(uint8_t step, uint8_t pressCount);

    /** Initiator: fails @p step when no result arrived in time. */
    void expireStep(uint8_t step);

    /** Initiator: fails unresolved steps, fixes the outcome, and sends OUTCOME reliably. */
    bool finishRound();

    /**
     * Ends the active session without counting it, tells the opponent with a reliable ABORT, and
     * raises the abort flag.
     */
    void abortRound();

    /** @return true after an abort, local or from the opponent, until clearAbort. */
    bool isAborted() const;

    /** Lowers the abort flag. */
    void clearAbort();

    /** @return true once OFFER and ACCEPT have been exchanged. */
    bool isHandshakeComplete() const;

    /** @return true once the round's OUTCOME is fixed (initiator) or received (responder). */
    bool isOutcomeKnown() const;

    /** Stores the record for a successful round and clears the session. @return false otherwise. */
    bool finalizeMatch();

    /**
     * Ends the round: stores the record on success, counts the attempt, remembers the score for
     * the result screen, and clears the session.
     */
    void concludeRound();

    /** Score of the last concluded round (outcome total, or local best guess without one). */
    uint8_t getLastRoundScore() const;

    /** @return true when the last concluded round reached NODE_SUCCESS_THRESHOLD. */
    bool wasLastRoundSuccess() const;

    /** Counts the outcome against the failed-attempt limit for the current peer. */
    void recordLinkOutcome(bool success);

    uint32_t getFailedAttempts() const;
    bool hasExhaustedAttempts() const;

    /** Remaining link retries before sleep (`0` when exhausted). */
    uint32_t getRetriesRemaining() const;

    DuelLinkSide getLocalLinkSide() const;

    /** Resets failed-attempt count (e.g. when entering sleep). */
    void clearFailedAttemptsForCurrentPeer();

    /** @return nullptr when no peer MAC has been tracked yet. */
    const uint8_t* getTrackedPeerMac() const;

    size_t getPendingRecordCount() const;
    bool readPendingRecord(size_t index, QuickdrawMatchRecord& record) const;
    void clearPendingRecords();

    /** Drops the active session; the failed-attempt count and abort flag are kept. */
    void clearCurrentMatch();

    const DuelSession& getSession() const;
private:
    bool buildLocalParticipant(DuelParticipant& participant) const;
    void trackPeer(const uint8_t* peerMac);
    void sendOffer();
    NodeNetworkPacket makeSessionPacket(NodeNetworkCommand command) const;
    void sendToOpponent(const NodeNetworkPacket& packet);
    void sendReliableToOpponent(const NodeNetworkPacket& packet);
    void sendReply(const uint8_t* destinationMac, NodeNetworkCommand command, const UUIDBytes& matchId,
                   int64_t unixMs) const;
    bool isOpponentSideSender(const NodeNetworkPacket& packet) const;
    /** @return true for a packet from the handshaken opponent's side of this session. */
    bool isRoundPacketFromOpponent(const uint8_t* senderMac, const NodeNetworkPacket& packet) const;
    uint32_t nowMs() const;

    void onOffer(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onAccept(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onTimeCorrection(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onSync(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onSyncReply(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onSequence(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onInputResult(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onInputPress(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onOutcome(const uint8_t* senderMac, const NodeNetworkPacket& packet);
    void onAbort(const uint8_t* senderMac, const NodeNetworkPacket& packet);

    PlayerProvider* playerProvider = nullptr;
    NodeNetworkWirelessManager* nodeNetworkWirelessManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    PlatformClock* platformClock = nullptr;
    MatchStorage* matchStorage = nullptr;
    DuelSession session;
    DuelLinkSide localLinkSide = DuelLinkSide::INITIATOR;
    bool localLinkSideAssigned = false;
    bool timeCorrectionApplied = false;
    bool hasTrackedPeer = false;
    uint8_t trackedPeerMac[6] = {};
    uint32_t failedAttempts = 0;
    bool aborted = false;
    uint8_t lastRoundScore = 0;
    bool lastRoundSuccess = false;
};
