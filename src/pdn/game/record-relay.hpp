#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/record-source.hpp"
#include "wireless/reliable-channel.hpp"

class MatchManager;
class ShootoutManager;

inline constexpr const char RECORD_RELAY_TAG[] = "RECORD_RELAY";

// One record kind's channels, frame in flight and last-sent identity; kinds
// share nothing, so one stuck kind never holds back the other.
template <class Traits> class RecordRelayLane {
public:
    using Frame = typename Traits::Frame;
    using PlayGate = std::function<bool()>;

    /**
     * Claims the kind's record and acknowledgement packet types. @p playIsActive
     * says whether a match or a shootout is active right now.
     */
    RecordRelayLane(WirelessManager* wirelessManager, PlayGate playIsActive)
        : wirelessManager(wirelessManager)
        , playIsActive(std::move(playIsActive))
        , frameChannel(wirelessManager, Traits::FRAME_PACKET_TYPE)
        , ackChannel(wirelessManager, Traits::ACK_PACKET_TYPE) {
        ackChannel.onReceive([this](const uint8_t* fromMac, const RecordAckPacket& ack) {
            onAcknowledgement(fromMac, ack);
        });
    }

    /** The lane's callbacks hold its address. */
    RecordRelayLane(const RecordRelayLane&) = delete;
    /** Not assignable, for the same reason. */
    RecordRelayLane& operator=(const RecordRelayLane&) = delete;

    /** Sets where this kind's records come from; null leaves the lane with nothing to send. */
    void setRecordSource(RecordSource<Traits>* source) { recordSource = source; }

    /**
     * Sends the lowest pending record to the FDN at @p fdnMac, unless play is
     * active, the radio is not in peer-to-peer mode, a frame of this kind is
     * still in flight, or nothing is pending.
     */
    void sendLowestPending(const uint8_t* fdnMac) { sendLowestPendingUnless(fdnMac, nullptr); }

    /**
     * Stops retrying the frame in flight. The record stays pending, and what was
     * last sent is kept: the FDN may already hold the frame, and its
     * acknowledgement still counts.
     */
    void cancelInFlight() { frameChannel.cancelAll(); }

private:
    struct LastSent {
        uint32_t recordNumber;
        uint8_t fdnMac[6];
        uint8_t recordCheck[RECORD_CHECK_SIZE];
    };

    // In flight means the transport is still retrying the frame. Once the radio
    // reports it delivered, or the transport gives up, the next trigger may send
    // the record again.
    bool isInFlight() const { return frameChannel.isPending(lastSent.fdnMac); }

    // @p fdnMac must not point into lastSent. A non-null @p acknowledgedNumber
    // names a record that is not sent even if the source still offers it.
    void sendLowestPendingUnless(const uint8_t* fdnMac, const uint32_t* acknowledgedNumber) {
        if (recordSource == nullptr || playIsActive()) {
            return;
        }
        // A frame offered while the radio is not in peer-to-peer mode is refused
        // on every retry, then abandoned.
        if (!wirelessManager->isEspNowReady()) {
            return;
        }
        if (isInFlight()) {
            return;
        }

        Frame frame{};
        uint32_t recordNumber = 0;
        if (!recordSource->readLowestPending(recordNumber, frame.content)) {
            return;
        }
        if (acknowledgedNumber != nullptr && recordNumber == *acknowledgedNumber) {
            // The mark did not move, most likely a failed write. Sending the
            // record straight back would have the FDN acknowledge it again, and
            // the two would repeat that as fast as the radio allows.
            LOG_E(RECORD_RELAY_TAG,
                  "Record %u of kind %u is still pending after its acknowledgement",
                  static_cast<unsigned>(recordNumber), static_cast<unsigned>(Traits::KIND));
            return;
        }
        frame.kind = static_cast<uint8_t>(Traits::KIND);
        frame.recordNumber = recordNumber;

        lastSent.recordNumber = recordNumber;
        memcpy(lastSent.fdnMac, fdnMac, sizeof(lastSent.fdnMac));
        computeRecordCheck(frame.content, lastSent.recordCheck);
        awaitingAcknowledgement = true;
        frameChannel.sendReliable(lastSent.fdnMac, frame);
    }

    // Only the acknowledgement for the frame as last sent counts: same kind,
    // number and record check, from the FDN it went to. Nothing here is keyed:
    // a match identifies the stored frame and proves nothing about the sender.
    void onAcknowledgement(const uint8_t* fromMac, const RecordAckPacket& ack) {
        if (recordSource == nullptr || !awaitingAcknowledgement) {
            return;
        }
        if (ack.kind != static_cast<uint8_t>(Traits::KIND) ||
            ack.recordNumber != lastSent.recordNumber ||
            memcmp(fromMac, lastSent.fdnMac, sizeof(lastSent.fdnMac)) != 0 ||
            memcmp(ack.recordCheck, lastSent.recordCheck, RECORD_CHECK_SIZE) != 0) {
            return;
        }

        awaitingAcknowledgement = false;
        // The radio's delivery report can be lost when the frame was not; a retry
        // still armed would send a record that is no longer pending.
        frameChannel.cancelAll();
        const uint32_t acknowledgedNumber = lastSent.recordNumber;
        LOG_I(RECORD_RELAY_TAG, "An FDN stored record %u of kind %u",
              static_cast<unsigned>(acknowledgedNumber), static_cast<unsigned>(Traits::KIND));
        recordSource->advanceMark(acknowledgedNumber);
        sendLowestPendingUnless(fromMac, &acknowledgedNumber);
    }

    WirelessManager* wirelessManager = nullptr;
    PlayGate playIsActive;
    RecordSource<Traits>* recordSource = nullptr;
    ReliableChannel<Frame> frameChannel;
    ReliableChannel<RecordAckPacket> ackChannel;
    // Held in memory only. After a reboot the first trigger sends the lowest
    // pending record again and the FDN acknowledges the copy it already holds.
    LastSent lastSent = {};
    // Set by a send and cleared by the acknowledgement that answers it, so one
    // acknowledgement advances the mark once.
    bool awaitingAcknowledgement = false;
};

/**
 * Delivers this PDN's records to FDNs over the peer-to-peer radio; the records
 * and their marks belong to each kind's RecordSource. The managers and the
 * coordinator are borrowed and must outlive it.
 */
class RecordRelay {
public:
    /**
     * Claims the four record packet types and the player-info beacon handler,
     * and starts observing jack connections, match starts and shootout phases.
     * @p matchManager is also the source of this device's match records.
     */
    RecordRelay(WirelessManager* wirelessManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
                MatchManager* matchManager, ShootoutManager* shootoutManager);
    /** Drops the beacon handler and the observers, which hold `this`. */
    ~RecordRelay();

    /** Registers the source of this device's crash records; null removes it. Not owned. */
    void setCrashRecordSource(RecordSource<CrashRecordTraits>* crashRecordSource);

private:
    bool isPlayActive() const;
    void onPlayerInfoBroadcast(const uint8_t* fromMac, const uint8_t* data, size_t length);
    void onJackChange(SerialIdentifier jack, bool connected);
    void sendPendingRecords(const uint8_t* fdnMac);
    void cancelSendsInFlight();

    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    MatchManager* matchManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    RecordRelayLane<CrashRecordTraits> crashLane;
    RecordRelayLane<MatchRecordTraits> matchLane;
};
