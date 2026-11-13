#pragma once

#include <cstddef>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/resender.hpp"

class WirelessManager;
class ReliableTransport;

// A reliable channel is a typed, reliable pipe for exactly one PktType: game
// code sends a packed payload struct and receives decoded structs back, and
// the channel supplies everything reliability needs underneath — seqId
// allocation, retry-until-delivered on the device's one ReliableTransport, and
// an abandon callback when a peer stays unreachable past the frame's span. The
// struct itself is the wire format (packed, memcpy'd); both ends run the same
// firmware, so field layout is the protocol. ReliableChannelBase holds the
// untyped mechanics and TypedChannelBase<P> binds them to a payload type; the
// two concrete channels below, ReliableChannel<P> and FanOutChannel<P>, differ
// in how a frame is addressed and how its delivery is proven.
//
// Constructing a channel is the whole registration: it claims its PktType on
// the WirelessManager's transport and installs the driver handlers, and its
// destructor drops every pending send and both handlers. A channel therefore
// lives exactly as long as the object that owns it, by value or by pointer.
//
// Delivery evidence: a ReliableChannel's unicast is cleared by the radio's
// SEND_SUCCESS; a FanOutChannel's broadcast by each recipient's kReliableAck,
// which only a FanOutChannel honours. Each type offers only its own reliable
// send, so a broadcast whose acks would be refused cannot be written.

class ReliableChannelBase {
public:
    using OnDelivered = std::function<void(uint8_t seqId, const uint8_t* toMac)>;

    /**
     * Claims `type` on wirelessManager's transport and installs the driver
     * receive and send-status handlers. With a null wirelessManager (probe-style
     * unit tests), or when `type` is already claimed on this device (a wiring
     * bug, logged), the channel runs without a radio: nothing is sent or received.
     */
    ReliableChannelBase(WirelessManager* wirelessManager, PktType type);
    /**
     * Drops every pending send on this PktType, detaches from the transport,
     * and clears both driver handlers.
     */
    virtual ~ReliableChannelBase();

    /** The PktType this channel claims. */
    PktType type() const { return packetType; }

    /** True when at least one send to this MAC is still awaiting its ack. */
    bool isPending(const uint8_t* mac) const;

    /**
     * Recipients of the frame sent under `seqId` that still owe an ack. Zero
     * once every one of them has answered or been given up on.
     */
    size_t pendingCount(uint8_t seqId) const;

    /** Silently drops every pending send to this MAC (no abandon callback). */
    void cancel(const uint8_t* mac);

    /** Silently drops every pending send on this channel, to every recipient. */
    void cancelAll();

    /**
     * Observes delivery to one peer: SEND_SUCCESS on a unicast, the peer's
     * kReliableAck on a fan-out. A late radio duplicate can re-fire it, so the
     * observer must be idempotent.
     */
    void setOnDelivered(OnDelivered cb) { onDeliveredCallback = std::move(cb); }

    /**
     * Typed subclass deserializes bytes into its payload type and dispatches
     * to its onReceive callback. Returns true if delivered.
     */
    virtual bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) = 0;

    /**
     * Radio send-result for one of this channel's outbound packets. The typed
     * subclass reads the stamped seqId back from `data` and, on success,
     * clears the pending entry.
     */
    virtual void onSendResult(const uint8_t* toMac, const uint8_t* data,
                              size_t len, bool success) = 0;

    /**
     * The transport gave up on `targetMac` for the frame sent under `seqId`;
     * `payload` is that frame, so the typed subclass can hand the decoded
     * struct to its abandon callback.
     */
    virtual void onAbandon(uint8_t seqId, const uint8_t* targetMac, const uint8_t* payload,
                           size_t len) = 0;

    /**
     * A kReliableAck from `fromMac` for the frame sent under `seqId`. Only a
     * FanOutChannel honours it, and only when it clears an obligation the
     * channel actually holds — anything else is noise off the air.
     */
    virtual void onPacketAck(const uint8_t* fromMac, uint8_t seqId) = 0;

protected:
    uint8_t nextSeqId();
    // Sends the bytes as given with no retry slot; the typed caller stamps seqId 0.
    void sendUnsequencedBytes(const uint8_t* mac, const uint8_t* data, size_t len);
    void ack(const uint8_t* toMac, uint8_t seqId);
    // Defined in the .cpp so the logger stays out of this template header.
    static void logLengthMismatch(PktType type, size_t got, size_t want);
    // seqId == 0 is the unsequenced sentinel (see nextSeqId) and is never deduped here.
    bool isDuplicateReliableRx(const uint8_t* fromMac, uint8_t seqId);

    // Both null on a channel that has no radio (see the constructor); every
    // send and ack then no-ops.
    Resender* resender;
    PktType packetType;
    WirelessManager* wirelessManager;
    OnDelivered onDeliveredCallback;

private:
    struct RxSeqRecord {
        std::array<uint8_t, 6> mac;
        uint8_t lastSeqId;
    };
    // Seeded at construction so a rebooted peer does not systematically restart
    // on the same seqId a receiver's cursor is already holding.
    uint8_t lastSentSeqId;
    // Last nonzero seqId delivered per sender. Receiving costs no peer-table
    // slot, so nothing bounds this but MAX_RX_SENDERS.
    std::vector<RxSeqRecord> rxSeq;
};

// A payload ending in a front-filled array may declare `size_t wireSize() const`
// to keep unused entries off the air; any other payload goes out whole.
template <class P, class = void> struct HasWireSize : std::false_type {};
template <class P>
struct HasWireSize<P, std::void_t<decltype(std::declval<const P&>().wireSize())>> : std::true_type {
};

// The payload-typed half both channel kinds share: wire encoding, decoding,
// the receive and abandon callbacks, and the unsequenced send.
template <class P> class TypedChannelBase : public ReliableChannelBase {
public:
    using OnReceive = std::function<void(const uint8_t* fromMac, const P&)>;
    using OnAbandon = std::function<void(uint8_t seqId, const uint8_t* targetMac, const P&)>;

    /**
     * Fire-and-forget: seqId 0 on the wire, no retry slot, no ack owed. For
     * frames a receiver must not act on late, where a retransmit would be worse
     * than a loss.
     */
    void sendUnsequenced(const uint8_t* mac, P& p) {
        p.seqId = 0;
        this->sendUnsequencedBytes(mac, reinterpret_cast<const uint8_t*>(&p), wireSizeOf(p));
    }

    /** Decodes the abandoned frame and hands it to the abandon callback. */
    void onAbandon(uint8_t seqId, const uint8_t* targetMac, const uint8_t* payload,
                   size_t len) override {
        P p;
        if (!onAbandonCallback || !decode(payload, len, p)) return;
        onAbandonCallback(seqId, targetMac, p);
    }

    /** Registers the decoded-payload handler. */
    void onReceive(OnReceive cb) { onReceiveCallback = std::move(cb); }

    /** Silently drops every pending send whose payload `matches` (no abandon callback). */
    void cancelMatching(const std::function<bool(const P&)>& matches) {
        if (this->resender == nullptr) return;
        this->resender->cancelMatching(this->packetType,
                                       [&matches](const uint8_t* payload, size_t len) {
                                           P typed;
                                           return decode(payload, len, typed) && matches(typed);
                                       });
    }

    /**
     * Registers the per-recipient give-up handler. Abandonment is a game-level
     * signal (void a match, abort a tournament), not a log line; cancel() drops
     * pending sends WITHOUT it.
     */
    void setOnAbandon(OnAbandon cb) { onAbandonCallback = std::move(cb); }

protected:
    TypedChannelBase(WirelessManager* wirelessManager, PktType type)
        : ReliableChannelBase(wirelessManager, type) {}

    // Decodes one inbound frame. A corrupt/truncated ESP-NOW frame (no CRC on
    // this path) is logged so it's diagnosable rather than silently dropped; a
    // frame that fit was decoded far enough to say how long it claims to be.
    bool decodeInbound(const uint8_t* data, size_t len, P& p) const {
        if (decode(data, len, p)) return true;
        logLengthMismatch(this->packetType, len, len <= sizeof(P) ? wireSizeOf(p) : sizeof(P));
        return false;
    }

    void dispatch(const uint8_t* fromMac, const P& p) const {
        if (onReceiveCallback) onReceiveCallback(fromMac, p);
    }

    static size_t wireSizeOf(const P& p) {
        if constexpr (HasWireSize<P>::value) {
            return p.wireSize();
        } else {
            return sizeof(P);
        }
    }

    // Fills `p` from a frame, zeroing whatever tail the sender left off. False
    // unless the frame is exactly as long as the payload it decodes to says.
    static bool decode(const uint8_t* data, size_t len, P& p) {
        if (len > sizeof(P)) return false;
        std::memcpy(&p, data, len);
        std::memset(reinterpret_cast<uint8_t*>(&p) + len, 0, sizeof(P) - len);
        return len == wireSizeOf(p);
    }

private:
    OnReceive onReceiveCallback;
    OnAbandon onAbandonCallback;
};

// Reliable unicast: each send names one peer, and the radio's SEND_SUCCESS
// for that peer is the delivery evidence.
template <class P> class ReliableChannel : public TypedChannelBase<P> {
public:
    /**
     * See ReliableChannelBase; the payload struct P is the wire format. `mode`
     * picks SUPERSEDE_PER_TARGET for a channel carrying current state (a newer
     * send to a peer obsoletes the prior unacked one, so an older retransmit
     * can never reinstate stale state) or KEEP_DISTINCT for one carrying items
     * of a stream (every send keeps its own retry slot, so a dropped earlier
     * item still retransmits).
     */
    ReliableChannel(WirelessManager* wirelessManager, PktType type,
                    Resender::SendMode mode = Resender::SendMode::SUPERSEDE_PER_TARGET)
        : TypedChannelBase<P>(wirelessManager, type)
        , sendMode(mode) {}

    /**
     * Reliable unicast: allocates a fresh nonzero seqId, writes it into `p`,
     * arms retry-until-ack, and returns the id. By reference rather than by
     * value so a 770-byte HeadTransferPayload is not copied onto this frame on
     * the way past; the id lands in the caller's struct as a consequence, which
     * costs nothing because every caller passes a local it built for this send.
     */
    uint8_t sendReliable(const uint8_t* mac, P& p) {
        p.seqId = this->nextSeqId();
        if (this->resender != nullptr) {
            this->resender->send(mac, this->packetType, p.seqId,
                                 reinterpret_cast<const uint8_t*>(&p), this->wireSizeOf(p),
                                 sendMode);
        }
        return p.seqId;
    }

    /**
     * Decode + dispatch one inbound packet, deduped on the sender's last seqId
     * first, because a sender that missed SEND_SUCCESS retransmits. Also the
     * untyped entry for the driver packet handler and the transport's
     * deliverIncoming seam.
     */
    bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) override {
        P p;
        if (!this->decodeInbound(data, len, p)) return false;
        if (this->isDuplicateReliableRx(fromMac, p.seqId)) return true;
        this->dispatch(fromMac, p);
        return true;
    }

    /**
     * Radio send-result for one of this channel's outbound packets. `data` is
     * the exact payload handed to the radio, so the stamped seqId reads straight
     * back out. SEND_SUCCESS clears the pending entry (the peer's MAC acked it)
     * and SEND_FAIL is left to the backoff timer.
     */
    void onSendResult(const uint8_t* toMac, const uint8_t* data, size_t len,
                      bool success) override {
        P p;
        if (!success || !this->decode(data, len, p)) return;
        if (p.seqId == 0) return;  // fire-and-forget, no pending entry to clear
        if (this->resender != nullptr) this->resender->onAck(this->packetType, p.seqId, toMac);
        // SEND_SUCCESS is the delivery signal whether or not a slot was still
        // armed: a late radio duplicate re-fires it, so observers are idempotent.
        if (this->onDeliveredCallback) this->onDeliveredCallback(p.seqId, toMac);
    }

    /**
     * Ignored: a unicast obligation is cleared by the radio alone, and anyone
     * in range can forge a kReliableAck naming this channel and a seqId read
     * off its frames.
     */
    void onPacketAck(const uint8_t*, uint8_t) override {}

private:
    Resender::SendMode sendMode;
};

// Reliable fan-out: one broadcast frame per send, each named recipient cleared
// by its own kReliableAck.
template <class P> class FanOutChannel : public TypedChannelBase<P> {
public:
    /** See ReliableChannelBase; the payload struct P is the wire format. */
    FanOutChannel(WirelessManager* wirelessManager, PktType type)
        : TypedChannelBase<P>(wirelessManager, type) {}

    /**
     * Reliable fan-out: allocates a fresh nonzero seqId, writes it into `p`,
     * sends one broadcast frame, and returns the id; each named recipient is
     * cleared by its own ack(). See Resender::sendBroadcast.
     */
    uint8_t sendBroadcast(const std::vector<std::array<uint8_t, 6>>& recipients, P& p) {
        p.seqId = this->nextSeqId();
        if (this->resender != nullptr) {
            this->resender->sendBroadcast(recipients, this->packetType, p.seqId,
                                          reinterpret_cast<const uint8_t*>(&p),
                                          this->wireSizeOf(p));
        }
        return p.seqId;
    }

    /**
     * Answers a frame received from `toMac`: sends the kReliableAck that clears
     * this recipient's slot on the sender. The receiving handler calls it only
     * for a frame it admitted, because an ack is a unicast and a unicast to a
     * stranger spends one of the radio's 20 peer-table slots.
     */
    using ReliableChannelBase::ack;

    /**
     * Decode + dispatch one inbound packet. Nothing is deduped on receipt:
     * distinct frames interleave on one PktType, and a duplicate still owes its
     * ack or the sender retries it to abandonment, so idempotence is left to the
     * handler, which has to see the duplicate to ack it. Also the untyped entry
     * for the driver packet handler and the transport's deliverIncoming seam.
     */
    bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) override {
        P p;
        if (!this->decodeInbound(data, len, p)) return false;
        this->dispatch(fromMac, p);
        return true;
    }

    /**
     * Clears nothing: a fan-out's send result names the broadcast address, and
     * its evidence arrives as kReliableAck packets instead (onPacketAck).
     */
    void onSendResult(const uint8_t*, const uint8_t*, size_t, bool) override {}

    /**
     * Clears `fromMac`'s slot for the frame sent under `seqId` and reports the
     * delivery, but only when this channel actually holds that obligation.
     */
    void onPacketAck(const uint8_t* fromMac, uint8_t seqId) override {
        if (this->resender == nullptr) return;
        if (!this->resender->onAck(this->packetType, seqId, fromMac)) return;
        if (this->onDeliveredCallback) this->onDeliveredCallback(seqId, fromMac);
    }
};
