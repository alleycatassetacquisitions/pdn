#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>
#include "device/serial-manager.hpp"
#include "device/serial-frame-parser.hpp"
#include "utils/callback-list.hpp"
#include "utils/simple-timer.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/reliable-channel.hpp"
#include "device/device-type.hpp"

#ifndef NATIVE_BUILD
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

class Device;
class HelloLinkMachine;
class RDCHelloTests;

enum class JackStatus {
    DISCONNECTED = 0,  // No HELLO peer on the jack.
    CONNECTING = 1,    // A HELLO peer is present, its context exchange unfinished.
    CONNECTED = 2,     // Jack is connected to a peer.
};

class RemoteDeviceCoordinator {
    // Lets a test put the published snapshot out of step with main-loop chain
    // state, which no public call can do.
    friend class RDCHelloTests;

public:
    /** Fires on a per-jack connect (true) / disconnect (false) transition. */
    using JackChangeCallback = std::function<void(SerialIdentifier jack, bool connected)>;
    /** Fires when this device's ChainRole changes. */
    using ChainRoleChangeCallback = std::function<void(ChainRole role)>;
    /**
     * Fires when the chain member roster changes; read the new roster via
     * getChainMembers(). Only a head holds a roster, but the last firing comes
     * as one hands its roster to a successor, so a handler may already read a
     * role that is no longer HEAD.
     */
    using MembershipChangeCallback = std::function<void()>;
    /**
     * Head-only: fires when the ring fully closes. This is the shootout
     * coordinator claim point.
     */
    using RingClosedCallback = std::function<void()>;

    /** Inert until initialize(). */
    RemoteDeviceCoordinator();
    /** Stops the emit task, unhooks the jacks and frees the link machines. */
    ~RemoteDeviceCoordinator();

    /**
     * Claims the RDC packet channels, then brings HELLO connectivity up on
     * every jack the SerialManager reports. Call once, before sync().
     */
    void initialize(WirelessManager* radio, SerialManager* serial, Device* device);

    /** Drives each jack's HELLO link machine; must be called every loop tick. */
    void sync(Device* PDN);

    /**
     * Connection state of one jack (device-level chain facts live in
     * getChainRole(), not here).
     */
    virtual JackStatus getJackStatus(SerialIdentifier jack);

    /**
     * Returns a pointer to the jack's direct peer MAC address, or nullptr when the
     * jack tracks no peer. Known from the first HELLO, so it is served from
     * CONNECTING onward, not only once CONNECTED.
     */
    virtual const uint8_t* getPeerMac(SerialIdentifier jack) const;

    /** The direct peer's hardware kind (PDN/FDN) for the given jack. */
    virtual DeviceType getPeerDeviceType(SerialIdentifier jack) const;

    /**
     * The direct peer's 4-digit player id for the given jack, lifted from the
     * PlayerProfile its context exchange delivered; PEER_USER_ID_NONE until then.
     * Gated on getPeerMac so the two never disagree about the jack having a peer.
     */
    virtual uint16_t getPeerUserId(SerialIdentifier jack) const {
        if (getPeerMac(jack) == nullptr) return PEER_USER_ID_NONE;
        return helloByJack[jackIndex(jack)].peerUserId;
    }

    /** Returns true iff `mac` matches the direct peer on either jack. */
    virtual bool isDirectPeer(const uint8_t* mac) const;

    /** MAC of the peer on the first CONNECTED jack whose peer is a `type`, or nullptr. */
    const uint8_t* getConnectedPeerMac(DeviceType type);

    /** True iff `mac` is the peer on a CONNECTED jack whose peer is a `type`. */
    bool isConnectedPeer(DeviceType type, const uint8_t* mac);

    // ---- Chain-level surface ----

    /** This device's chain role, derived from its PDN-to-PDN jack links plus the ring latch. */
    virtual ChainRole getChainRole() const;

    /**
     * The chain head's MAC, or nullptr when this device is the head or
     * standalone. Also nullptr for a CHILD during re-convergence: after an
     * OUTPUT-side ring break the latch clears while INPUT stays connected, and
     * no replacement head has propagated yet.
     */
    virtual const uint8_t* getHeadMac() const;

    /** Head-only chain member roster; empty for a child or standalone device. */
    virtual std::vector<std::array<uint8_t, 6>> getChainMembers() const;

    /**
     * True on EVERY device sitting on a closed ring, not just the one that
     * detected the closure. The detecting device latches locally; the rest
     * learn it from the HELLO ring flag relayed down the chain.
     */
    virtual bool isInRing() const;

    /**
     * Registers `owner`'s per-jack connect/disconnect observer; a null callback
     * removes it and no other owner's. The connect fires after the chain state
     * it implies is in place; the disconnect fires BEFORE chain-state teardown,
     * so handlers must not read chain state there — consistent chain facts
     * arrive via setOnChainRoleChange.
     */
    void setOnJackChange(JackChangeCallback callback, const void* owner) {
        jackChangeCallbacks.set(std::move(callback), owner);
    }

    // ---- Per-jack HELLO connectivity ----
    // HELLO is the only thing serial carries: discovery, liveness and hop-by-hop
    // chain context. Each jack runs an independent link SM fed by the real
    // exec()-driven RX byte pump.

    // The jacks that can carry a HELLO link, in the order sync() drives them.
    static constexpr std::array<SerialIdentifier, 3> HELLO_JACKS = {
        SerialIdentifier::OUTPUT_JACK,
        SerialIdentifier::INPUT_JACK,
        SerialIdentifier::INPUT_JACK_SECONDARY,
    };

    static constexpr unsigned long HELLO_CADENCE_MS = 20;
    static constexpr unsigned long HELLO_SILENT_LINK_MS = 100;
    // A link stuck mid-context-exchange past this falls back to IDLE.
    static constexpr unsigned long CONTEXT_EXCHANGE_TIMEOUT_MS = 500;
    // A ring latch is evidence-based: it survives a higher-MAC head claim only
    // while this device's own MAC keeps returning on INPUT within this window, so
    // it has to outlast a replacement claim's walk round the ring. Hand-set at 25
    // HELLO cadences, not derived: a hop also costs the receiving device's
    // exec()-to-parse gap, which nothing caps, so this is slack rather than
    // arithmetic and not a bound on any particular chain length. Deliberately not
    // sized off MAX_CHAIN_MEMBERS, a roster-array cap set past any real chain.
    static constexpr unsigned long RING_EVIDENCE_TIMEOUT_MS = 500;

    enum class HelloLinkState { IDLE,
                                CONNECTING,
                                CONNECTED };

    // Sized to the larger of the two context profiles so a stored profile is never
    // truncated; both are packed wire structs, so sizeof is the wire length.
    static constexpr size_t MAX_PEER_PROFILE_BYTES =
        std::max(sizeof(PlayerProfile), sizeof(FdnProfile));

    /**
     * Hands a received peer context to the game layer opaquely: the jack it
     * arrived on, the peer's device kind, and the raw profile bytes
     * (PlayerProfile for PDN, FdnProfile for FDN). RDC never interprets them.
     */
    using ContextReceivedCallback =
        std::function<void(SerialIdentifier jack, DeviceType peerType,
                           const uint8_t* profile, size_t len)>;
    /** Registers `owner`'s opaque peer-context consumer; null removes it. */
    void setOnContextReceived(ContextReceivedCallback callback, const void* owner) {
        contextReceivedCallbacks.set(std::move(callback), owner);
    }

    /**
     * Yields this device's outgoing player profile, forwarded verbatim in the
     * context this device sends. Opaque to RDC.
     */
    using SelfProfileProvider = std::function<PlayerProfile()>;

    /**
     * Registers the outgoing-profile source. A provider rather than a stored
     * copy because the id, name, faction and role each settle at a different
     * point in registration: reading at send time keeps a context honest
     * without every one of those setters having to re-push here.
     */
    void setSelfProfileProvider(SelfProfileProvider provider) {
        selfProfileProvider = std::move(provider);
    }

    /**
     * Queues a fresh context to the peer on every Connected jack. The exchange
     * is otherwise once-per-connect, so a profile field a peer acts on (the
     * hunter/bounty role) changing on an already-open link has to push itself
     * or the peer keeps acting on the stale value until the next replug.
     */
    void resendContext();

    /**
     * The peer's own ChainRole as of the context it sent on `jack`, recorded but
     * not acted on here. STANDALONE before any context arrives: a peer whose only
     * link is the one still connecting reports exactly that.
     */
    ChainRole getPeerChainRole(SerialIdentifier jack) const {
        return helloByJack[jackIndex(jack)].peerChainRole;
    }

    /**
     * The peer's opaque profile bytes on `jack`, or nullptr before its context
     * arrives. PlayerProfile for a PDN peer, FdnProfile for an FDN one; which it
     * is comes from getPeerDeviceType. RDC never interprets them. Held per jack
     * rather than only forwarded on arrival, so a state mounting later still gets
     * the peer's identity instead of only whoever was mounted at the time.
     */
    virtual const uint8_t* getPeerProfile(SerialIdentifier jack, size_t& length) const {
        const JackHelloLink& link = helloByJack[jackIndex(jack)];
        length = link.peerProfileLen;
        return link.peerProfileLen == 0 ? nullptr : link.peerProfile.data();
    }

    /** True while a context send to `mac` is still awaiting its SEND_SUCCESS. */
    bool isContextSendPending(const uint8_t* mac) const;

    /**
     * Native/test hook: suppress the FreeRTOS emit-task spawn so the caller
     * drives emitHello() and the per-jack link machines (via sync()) on one thread.
     */
    void setExternalConnectivityTask(bool external) { externalConnectivityTask = external; }

    /**
     * Emits one HELLO frame on every jack from the published atomic snapshot;
     * touches no main-loop state and fires no callback.
     */
    void emitHello();

#ifndef NATIVE_BUILD
    /**
     * FreeRTOS task body: emit at HELLO_CADENCE_MS until the destructor requests
     * a stop, then self-delete. A member so it can read the stop flags directly.
     */
    void connectivityTaskBody();
#endif

    /**
     * Driven by a completed context exchange: CONNECTING->CONNECTED, whose mount
     * fires the jack-connect observer.
     */
    void onContextExchangeComplete(SerialIdentifier jack);

    /** This jack's HELLO link state (observability / tests). */
    HelloLinkState getHelloLinkState(SerialIdentifier jack) const;

    /** Registers `owner`'s chain-role transition observer; null removes it. */
    void setOnChainRoleChange(ChainRoleChangeCallback callback, const void* owner) {
        chainRoleChangeCallbacks.set(std::move(callback), owner);
    }

    /** Registers `owner`'s head-only roster observer; null removes it. */
    void setOnMembershipChange(MembershipChangeCallback callback, const void* owner) {
        membershipChangeCallbacks.set(std::move(callback), owner);
    }

    /** Registers `owner`'s head-only ring-closure observer; null removes it. */
    void setOnRingClosed(RingClosedCallback callback, const void* owner) {
        ringClosedCallbacks.set(std::move(callback), owner);
    }

    /**
     * Registers `owner`'s observer for any jack connect/disconnect; null removes
     * it. Coarser than setOnJackChange: it says the chain moved, not which jack
     * or which way.
     */
    void setChainChangeCallback(std::function<void()> callback, const void* owner);

protected:
    /**
     * Tells every jack-change observer about one edge. The link machines are
     * its source here; a coordinator double that stands in for them calls it too.
     */
    void fireJackChange(SerialIdentifier jack, bool connected) {
        jackChangeCallbacks.fire(jack, connected);
    }

private:
    static constexpr size_t NUM_JACKS = 3;

    size_t jackIndex(SerialIdentifier jack) const;

    void recordJackContext(SerialIdentifier jack, DeviceType peerType, ChainRole chainRole,
                           uint16_t peerUserId, const uint8_t* profile, size_t len);

    SerialManager* serialManager = nullptr;
    WirelessManager* wirelessManager = nullptr;
    CallbackList<std::function<void()>> chainChangeCallbacks;
    CallbackList<JackChangeCallback> jackChangeCallbacks;
    CallbackList<ChainRoleChangeCallback> chainRoleChangeCallbacks;
    CallbackList<MembershipChangeCallback> membershipChangeCallbacks;
    CallbackList<RingClosedCallback> ringClosedCallbacks;

    // ---- HELLO connectivity internals ----
    struct JackHelloLink {
        SerialFrameParser parser;  // non-copyable; default-constructed in place
        HelloLinkMachine* machine = nullptr;  // per-jack link SM; owned, deleted in dtor
        ChainRole peerChainRole = ChainRole::STANDALONE;
        // Which context channel the peer's context arrived on, so the kind and the
        // profile bytes below always describe the same peer and the same moment.
        DeviceType peerDeviceType = DeviceType::UNKNOWN;
        std::array<uint8_t, MAX_PEER_PROFILE_BYTES> peerProfile{};
        size_t peerProfileLen = 0;
        // The only profile field lifted onto the jack surface; the profile itself
        // stays opaque and reaches the game layer as raw bytes.
        uint16_t peerUserId = PEER_USER_ID_NONE;
        SimpleTimer contextResendThrottleTimer;
    };
    std::array<JackHelloLink, NUM_JACKS> helloByJack;
    bool externalConnectivityTask = false;
    CallbackList<ContextReceivedCallback> contextReceivedCallbacks;
    SelfProfileProvider selfProfileProvider;
    std::array<uint8_t, 6> selfMac{};
    DeviceType selfDeviceType = DeviceType::UNKNOWN;

    // Context channels are device-level, not per-jack: on receive the source MAC
    // is matched back to a jack. Constructed in initialize() once the
    // WirelessManager is known.
    ReliableChannel<PdnConnectionContext>* pdnContextChannel = nullptr;
    ReliableChannel<FdnConnectionContext>* fdnContextChannel = nullptr;
    ReliableChannel<ConnectionAnnouncePayload>* connectionAnnounceChannel = nullptr;
    ReliableChannel<DisconnectReportPayload>* disconnectReportChannel = nullptr;
    ReliableChannel<HeadTransferPayload>* headTransferChannel = nullptr;

    // A context (ESP-NOW) can beat its own HELLO (serial) and arrive before any jack
    // is CONNECTING for that MAC. Rather than drop it, hold it here and apply it when
    // the jack connects. Bounded + TTL'd on purpose: an on-channel attacker who knows
    // our MAC can spray contexts from fabricated sources, so the fixed cap is the OOM
    // guard and the short TTL clears junk; the worst it can do is evict a genuine
    // early-arrival (that link then half-opens, same as with no buffer). Legitimate
    // occupancy is at most one per jack.
    static constexpr size_t CONTEXT_BUFFER_SLOTS = 8;
    // TTL = silent-link window on purpose: a live peer's HELLO lands inside that
    // window, so a context older than it can never pair with a connecting jack.
    static constexpr unsigned long CONTEXT_BUFFER_TTL_MS = HELLO_SILENT_LINK_MS;
    struct BufferedContext {
        std::array<uint8_t, 6> mac{};
        DeviceType peerType = DeviceType::UNKNOWN;
        ChainRole chainRole = ChainRole::STANDALONE;
        uint16_t peerUserId = PEER_USER_ID_NONE;
        std::array<uint8_t, MAX_PEER_PROFILE_BYTES> profile{};
        size_t len = 0;
        unsigned long arrivedAtMs = 0;
        bool valid = false;
        bool isExpiredAt(unsigned long now) const {
            return now - arrivedAtMs > CONTEXT_BUFFER_TTL_MS;
        }
    };
    std::array<BufferedContext, CONTEXT_BUFFER_SLOTS> contextBuffer;

    // A jack entering Connecting initiates: apply any context buffered for its peer,
    // then reliably send this device's context (skipped if our other jack already
    // has a send pending to that same peer).
    void initiateContextExchange(SerialIdentifier jack);
    // Serialize + reliably send this device's context to `mac` per selfDeviceType.
    void sendSelfContext(const uint8_t* mac);
    void onContextReceived(const uint8_t* fromMac, DeviceType peerType, ChainRole chainRole,
                           uint16_t peerUserId, const uint8_t* profile, size_t len);
    void applyContextToJacks(const uint8_t* fromMac, DeviceType peerType, ChainRole chainRole,
                             uint16_t peerUserId, const uint8_t* profile, size_t len);
    // Assumes `jack` is CONNECTING to this peer; both timing paths route through here,
    // so the context callback fires once per connect.
    void completeJackContext(SerialIdentifier jack, DeviceType peerType, ChainRole chainRole,
                             uint16_t peerUserId, const uint8_t* profile, size_t len);
    // Holds a context whose jack is not yet CONNECTING; evicts oldest when full.
    void bufferContext(const uint8_t* fromMac, DeviceType peerType, ChainRole chainRole,
                       uint16_t peerUserId, const uint8_t* profile, size_t len);
    // Applies any cached context for `jack`'s peer to `jack` as it connects. Leaves
    // the cache entry for the peer's other jack (2-node ring); the TTL clears it.
    void drainBufferedContext(SerialIdentifier jack, const uint8_t* mac);
    // Link death on `jack`: clear the jack's peer residue; kill its context retries
    // unless another jack still faces this peer.
    void releaseHelloPeer(SerialIdentifier jack, const uint8_t* mac);
#ifndef NATIVE_BUILD
    TaskHandle_t connectivityTaskHandle = nullptr;
    // Cooperative stop: the destructor sets stopRequested and waits for the task
    // to observe it and self-delete (setting exited), rather than a cross-core
    // vTaskDelete that could truncate an in-flight emit or strand the UART lock.
    std::atomic<bool> connectivityTaskStopRequested{false};
    std::atomic<bool> connectivityTaskExited{false};
    static constexpr uint32_t kConnectivityTaskStack = 4096;
    static constexpr unsigned kConnectivityTaskPriority = 1;
#endif

    /**
     * Wires byte callbacks + parsers on every present jack and (unless the
     * caller drives it) spawns the emit task. Called once, from initialize().
     */
    void enableHelloConnectivity();

    HWSerialWrapper* jackWrapper(SerialIdentifier jack) const;
    void onHelloReceived(SerialIdentifier jack, const HelloPayload& hello);
    static unsigned long nowMs();

    // ---- Device-level chain state machine ----
    // Main-loop-owned; chainHeadMac is all-zero when no upstream head is held.
    std::array<uint8_t, 6> chainHeadMac{};
    bool chainHeadConfirmed = false;
    // Latched when this structural head sees its own MAC return on the INPUT jack;
    // cleared when any ring link drops. Dominates getChainRole().
    bool ringLatched = false;
    // The ring flag the INPUT peer last advertised. Only the latching device can
    // observe closure, so every other member learns it from upstream and relays
    // it on. Relayed only while a head is held, which keeps the assertion
    // single-sourced: the latching device holds no head, so its own flag is
    // never fed back to it and a cleared latch drains round the ring.
    bool upstreamRingClosed = false;
    // Written only by publishHelloState() on the main loop so emitHello() reads
    // a consistent view in one load.
    static constexpr uint64_t HEAD_MAC_MASK = 0xFFFFFFFFFFFFULL;
    static constexpr uint64_t CONFIRMED_BIT = 1ULL << 48;
    static constexpr uint64_t RING_CLOSED_BIT = 1ULL << 49;
    std::atomic<uint64_t> publishedHelloState{0};
    // Last time the own-MAC head returned on INPUT while latched (seeded at
    // closure): the ring-latch evidence stamp checked against RING_EVIDENCE_TIMEOUT_MS.
    unsigned long lastSelfHeadReturnMs = 0;
    // Last role reported to chainRoleChangeCallback, for edge-triggered firing.
    ChainRole lastChainRole = ChainRole::STANDALONE;
    // The effective head the INPUT peer last advertised (all-zero = none), kept
    // apart from chainHeadMac: a HELLO can advertise a head long before the
    // link carrying it is established, and only the latter may be acted on.
    std::array<uint8_t, 6> upstreamAdvertisedHead{};

    static void unpackMac(uint64_t value, uint8_t* out);
    void publishHelloState();
    // A Connected jack counts toward the chain unless either end is an FDN, which
    // is a jack peer for symbol match and never a chain member.
    bool isChainLink(SerialIdentifier jack) const;
    // Ring detection and the latched-head conflict, driven by every INPUT HELLO a
    // PDN hears; taking on a foreign head is left to adoptUpstreamHead.
    void applyUpstreamHead(const HelloPayload& hello);
    // Adopts upstreamAdvertisedHead once the INPUT jack is a chain link: hands
    // over the roster and announces. No-op before that.
    void adoptUpstreamHead();
    void onLinkLost(SerialIdentifier jack);
    void maybeFireChainRoleChange();
    // Roster traffic is addressed to the head as head, so it dies with the role.
    // A surviving retransmit would address a device that is no longer the head.
    void cancelHeadRosterTraffic(const uint8_t* headMac);

    // ---- Head roster ----
    // member MAC -> its direct upstream MAC. Only the roster authority (see
    // isRosterAuthority) accepts roster traffic or serves getChainMembers.
    std::map<std::array<uint8_t, 6>, std::array<uint8_t, 6>> chainRoster;
    // seqId of the announce most recently sent; delivery of any earlier one is
    // stale and must not raise the confirmed bit.
    uint8_t pendingAnnounceSeqId = 0;
    // disconnectedMac of the report most recently sent whose delivery is not yet
    // confirmed (all-zero = none). A head change cancels the in-flight report to
    // the old head, and unlike the announce path nothing re-triggers it, so
    // adoptUpstreamHead re-sends it to the successor — otherwise the departed
    // member stays a phantom in the new head's roster.
    std::array<uint8_t, 6> pendingReportMac{};
    uint8_t pendingReportSeqId = 0;
    bool isRosterAuthority() const;
    // Send ConnectionAnnounce{INPUT peer} to the held head, once the upstream
    // context exchange has completed. No-op for a head/ring/standalone device.
    void maybeAnnounceToHead();
    // OUTPUT (downstream) link death: report to the head (unless the lost link
    // IS the head), or clear the roster when this device is the head.
    void reportDownstreamLoss(const uint8_t* lostMac);
    // Reliable-send a DisconnectReport naming `lostMac` to `headMac`, tracking
    // it as the pending report until its delivery is confirmed.
    void sendDisconnectReport(const uint8_t* headMac, const uint8_t* lostMac);
    // Stop retrying every pending report that names `returnedMac`: delivered after
    // that peer re-announces, it would erase a live member.
    void retractReportFor(const uint8_t* returnedMac);
    void clearPendingReport();
    // Drop `mac` and every member whose upstream chain passes through it.
    void removeChainMemberSubtree(const uint8_t* mac);
    // One OUTPUT jack means one downstream: when `member` announces `upstream`,
    // any OTHER member still recorded on that upstream is stale — remove it
    // and its subtree before the new claim lands.
    void evictStaleUpstreamClaimant(const std::array<uint8_t, 6>& member,
                                    const std::array<uint8_t, 6>& upstream);
    // True when some member other than `member` already holds `upstream`. The
    // transfer path queries this to skip (not evict) a colliding insert.
    bool isUpstreamHeldByOther(const std::array<uint8_t, 6>& upstream,
                               const std::array<uint8_t, 6>& member) const;
    // Demotion handoff: unicast the roster to the successor head, then clear it.
    void transferRosterTo(const uint8_t* newHeadMac);
    void onConnectionAnnounce(const uint8_t* fromMac, const ConnectionAnnouncePayload& announce);
    void onDisconnectReport(const uint8_t* fromMac, const DisconnectReportPayload& report);
    void onHeadTransfer(const uint8_t* fromMac, const HeadTransferPayload& transfer);
};
