#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/crc16.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/record-source.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

class MatchManager;
class ShootoutManager;

inline constexpr const char RECORD_RELAY_TAG[] = "RECORD_RELAY";

// One record kind's frame channel and the identity of the frame last sent.
// In-flight state is keyed by packet type, so one stuck kind never holds back the other.
template <class Traits> class RecordRelayLane {
public:
    using Frame = typename Traits::Frame;

    /**
     * Claims the kind's frame packet type on the radio; @p resender, which the
     * owner syncs, carries its retries. @p source may be null until setRecordSource.
     */
    RecordRelayLane(WirelessManager* wirelessManager, Resender* resender,
                    RecordSource<Traits>* source)
        : recordSource(source)
        , frameChannel(wirelessManager, resender, Traits::FRAME_PACKET_TYPE, nullptr) {}

    /** The channel registers its own address with the radio driver. */
    RecordRelayLane(const RecordRelayLane&) = delete;
    /** Not assignable, for the same reason. */
    RecordRelayLane& operator=(const RecordRelayLane&) = delete;

    /** Sets where this kind's records come from; null leaves nothing to send. Not owned. */
    void setRecordSource(RecordSource<Traits>* source) { recordSource = source; }

    /**
     * Sends the lowest pending record to the FDN at @p fdnMac, unless a frame of
     * this kind is still in flight or nothing is pending.
     */
    void sendLowestPending(const uint8_t* fdnMac) {
        // In flight means the transport is still retrying the frame. Once the
        // radio reports it delivered, or the transport gives up, the next beacon
        // may send the record again.
        if (recordSource == nullptr || frameChannel.isPending(lastSent.fdnMac)) return;

        Frame frame{};
        uint32_t recordNumber = 0;
        if (!recordSource->readLowestPending(recordNumber, frame.content)) return;
        frame.kind = static_cast<uint8_t>(Traits::KIND);
        frame.recordNumber = recordNumber;

        lastSent.recordNumber = recordNumber;
        memcpy(lastSent.fdnMac, fdnMac, sizeof(lastSent.fdnMac));
        lastSent.contentCrc = crc16(frame.content, sizeof(frame.content));
        awaitingAcknowledgement = true;
        frameChannel.sendReliable(lastSent.fdnMac, frame);
    }

    /**
     * Retires the record last sent if @p ack names it by number and content
     * check and comes from the FDN it went to. The check identifies the stored
     * frame and proves nothing about the sender.
     * @return true when the record was retired and the next may be sent.
     */
    bool retire(const uint8_t* fromMac, const RecordAckPacket& ack) {
        if (recordSource == nullptr || !awaitingAcknowledgement ||
            ack.recordNumber != lastSent.recordNumber || ack.contentCrc != lastSent.contentCrc ||
            memcmp(fromMac, lastSent.fdnMac, sizeof(lastSent.fdnMac)) != 0) {
            return false;
        }
        awaitingAcknowledgement = false;
        // The delivery report can be lost when the frame was not; a retry still
        // armed would resend a record that is no longer pending.
        frameChannel.cancel(lastSent.fdnMac);
        if (!recordSource->advanceMark(lastSent.recordNumber)) {
            // Sending on would offer the same record, and the FDN would ack it
            // again, as fast as the radio allows.
            LOG_E(RECORD_RELAY_TAG, "Record %u of kind %u is still pending after its ack",
                  static_cast<unsigned>(lastSent.recordNumber),
                  static_cast<unsigned>(Traits::KIND));
            return false;
        }
        LOG_I(RECORD_RELAY_TAG, "An FDN stored record %u of kind %u",
              static_cast<unsigned>(lastSent.recordNumber), static_cast<unsigned>(Traits::KIND));
        return true;
    }

private:
    struct LastSent {
        uint32_t recordNumber;
        uint8_t fdnMac[6];
        uint16_t contentCrc;
    };

    RecordSource<Traits>* recordSource = nullptr;
    ReliableChannel<Frame> frameChannel;
    // Held in memory only. After a restart a later beacon sends the lowest
    // pending record again and the FDN acks the copy it already holds.
    LastSent lastSent = {};
    // Set by a send and cleared by the ack that answers it, so one ack
    // advances the mark once.
    bool awaitingAcknowledgement = false;
};

/**
 * Delivers this PDN's records to FDNs over the peer-to-peer radio, one frame
 * per kind in flight, when it hears an FDN's beacon: crash records from the
 * source set here, match records from the MatchManager's ring. The managers are
 * borrowed and must outlive it; sync() must run every tick.
 */
class RecordRelay {
public:
    /** Claims the record frame and ack packet types and the beacon handler. */
    RecordRelay(WirelessManager* wirelessManager, MatchManager* matchManager,
                ShootoutManager* shootoutManager);
    /** Drops the beacon handler, which holds `this`; the channels drop their own. */
    ~RecordRelay();

    /** Registers the source of this device's crash records; null removes it. Not owned. */
    void setCrashRecordSource(RecordSource<CrashRecordTraits>* crashRecordSource);

    /** Drives the record frames' retries. */
    void sync() { resender.sync(); }

private:
    // No new send while a match or a shootout is active, or while the radio is
    // not in peer-to-peer mode, where a frame is refused on every retry.
    bool isSendingAllowed();
    void onPlayerInfoBroadcast(const uint8_t* fromMac, const uint8_t* data, size_t length);
    void onAcknowledgement(const uint8_t* fromMac, const RecordAckPacket& ack);

    WirelessManager* wirelessManager = nullptr;
    MatchManager* matchManager = nullptr;
    ShootoutManager* shootoutManager = nullptr;
    // Declared before the channels, which hold it and are destroyed first.
    Resender resender;
    RecordRelayLane<CrashRecordTraits> crashLane;
    RecordRelayLane<MatchRecordTraits> matchLane;
    ReliableChannel<RecordAckPacket> ackChannel;
};
