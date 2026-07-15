#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include "device/serial-manager.hpp"
#include "device/serial-frame-parser.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "wireless/reliable-transport.hpp"
#include "device/device-type.hpp"

#ifndef NATIVE_BUILD
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

class Device;
class HandshakeApp;
class HelloLinkMachine;
class RDCHelloTests;  // drives the context exchange via the owned transport in tests

enum class PortStatus {
    DISCONNECTED = 0,  // No Connection. Handshake is in Idle state.
    CONNECTING = 1,    // Port is NOT connected && NOT in handshake idle state.
    CONNECTED = 2,     // Port is connected to a peer.
};

// This device's position in the physical chain, derived from jack state:
// head has no in-peer, a child has one, a ring is a chain whose ends met.
// Device-level (not per-port): whether a connection makes this device a head
// or child is a chain concern surfaced here, never a PortStatus.
enum class ChainRole {
    STANDALONE = 0,
    HEAD = 1,
    CHILD = 2,
    RING = 3,
};

struct PortState {
    SerialIdentifier port;
    PortStatus status;
    std::vector<std::array<uint8_t, 6>> peerMacAddresses;
};

class RemoteDeviceCoordinator {
    // Reaches the owned transport to inject SEND_SUCCESS / inbound contexts without
    // a radio; the transport stays out of the public API.
    friend class RDCHelloTests;

public:
    /// Fires on a per-jack connect (true) / disconnect (false) transition.
    using JackChangeCallback = std::function<void(SerialIdentifier jack, bool connected)>;
    /// Fires when this device's ChainRole changes.
    using ChainRoleChangeCallback = std::function<void(ChainRole role)>;
    /// Head-only: fires when the chain member roster changes; read the new
    /// roster via getChainMembers().
    using MembershipChangeCallback = std::function<void()>;
    /// Head-only: fires when the ring fully closes. This is the shootout
    /// coordinator claim point.
    using RingClosedCallback = std::function<void()>;

    /// Inert until initialize().
    RemoteDeviceCoordinator();
    /// Tears down the per-port handshake apps.
    ~RemoteDeviceCoordinator();

    /**
     * Initialize should create a HandshakeWirelessManager, as well as
     * the handshake state machines. It should also
     * register the handshakeWirelessManager's packet received callback.
     */
    void initialize(WirelessManager* wirelessManager,
                    SerialManager* serialManager,
                    Device* PDN);

    /**
     * Must be called every loop tick (from PDN::loop).
     * Drives both handshake state machines.
     */
    void sync(Device* PDN);

    /// Connection state of one jack (device-level chain facts live in
    /// getChainRole(), not here).
    virtual PortStatus getPortStatus(SerialIdentifier port);
    /// Status plus the peer MACs reachable via the port.
    PortState getPortState(SerialIdentifier port);

    /**
     * Returns a pointer to the peer's MAC address for the given port, or nullptr if no peer is connected.
     * Prefer this over getPortState() when only the MAC address is needed.
     */
    virtual const uint8_t* getPeerMac(SerialIdentifier port) const;

    /// The direct peer's hardware kind (PDN/FDN) for the given port.
    virtual DeviceType getPeerDeviceType(SerialIdentifier port) const;

    /// The direct peer's 4-digit player id for the given port; 0xFFFF when
    /// unregistered or no peer. STUB until the context exchange lands (#157):
    /// always 0xFFFF.
    virtual uint16_t getPeerUserId(SerialIdentifier port) const { return 0xFFFF; }

    /// Returns true iff `mac` matches the direct peer on either jack.
    virtual bool isDirectPeer(const uint8_t* mac) const;

    /// Reachable via either jack (direct peer or daisy-chained).
    virtual bool canReachPeer(const uint8_t* mac) const;

    // ---- Chain-level surface (#154) ----

    /// This device's chain role, derived from jack presence plus the ring latch.
    virtual ChainRole getChainRole() const;

    /// The chain head's MAC, or nullptr when this device is the head or
    /// standalone. Also nullptr for a CHILD during re-convergence: after an
    /// OUTPUT-side ring break the latch clears while INPUT stays connected, and
    /// no replacement head has propagated yet.
    virtual const uint8_t* getHeadMac() const;

    /// Head-only chain member roster; empty for a child or standalone device.
    virtual std::vector<std::array<uint8_t, 6>> getChainMembers() const;

    /// Registers the per-jack connect/disconnect observer. The disconnect fires
    /// before chain-state teardown, so handlers must not read chain state here;
    /// consistent chain facts arrive via setOnChainRoleChange.
    void setOnJackChange(JackChangeCallback callback) {
        jackChangeCallback = std::move(callback);
    }

    // ---- Per-jack HELLO connectivity (#155) ----
    // HELLO is the serial discovery/liveness beacon that replaces the string
    // handshake (deleted by #160). Each jack runs an independent link SM fed by
    // the real exec()-driven RX byte pump. Off by default: production stays on
    // the handshake until the context exchange (#157) and device chain SM (#156)
    // land, since nothing drives CONNECTING->CONNECTED before then.

    static constexpr unsigned long HELLO_CADENCE_MS = 20;
    static constexpr unsigned long HELLO_SILENT_LINK_MS = 100;
    // A link stuck mid-context-exchange past this falls back to IDLE (#157).
    static constexpr unsigned long CONTEXT_EXCHANGE_TIMEOUT_MS = 500;
    // A ring latch is evidence-based: it survives a higher-MAC head claim only
    // while this device's own MAC keeps returning on INPUT within this window.
    // Sized above the worst-case claim-propagation transient (~18 hops at the
    // 20ms HELLO cadence).
    static constexpr unsigned long RING_EVIDENCE_TIMEOUT_MS = 500;

    enum class HelloLinkState { IDLE,
                                CONNECTING,
                                CONNECTED };

    /// Hands a received peer context to the game layer opaquely: the jack it
    /// arrived on, the peer's device kind, and the raw profile bytes
    /// (PlayerProfile for PDN, FdnProfile for FDN). RDC never interprets them.
    using ContextReceivedCallback =
        std::function<void(SerialIdentifier jack, DeviceType peerType,
                           const uint8_t* profile, size_t len)>;
    /// Registers the opaque peer-context consumer.
    void setOnContextReceived(ContextReceivedCallback callback) {
        contextReceivedCallback = std::move(callback);
    }

    /// Supplies this device's outgoing player profile, forwarded verbatim in the
    /// context this device sends to a new peer on any jack. Opaque to RDC.
    void setSelfPlayerProfile(const PlayerProfile& profile) {
        selfPlayerProfile = profile;
    }

    /// The chainRole recorded from the peer's context on `jack`; 0 until one
    /// arrives. Recorded for the device chain SM (#156), not acted on here.
    uint8_t getPeerChainRole(SerialIdentifier jack) const {
        return helloByPort[portIndex(jack)].peerChainRole;
    }

    /// True while a context send to `mac` is still awaiting its SEND_SUCCESS.
    bool isContextSendPending(const uint8_t* mac) const;

    /// Wires byte callbacks + parsers on every present jack, quiesces the
    /// handshake, and (unless external) spawns the emit task. Idempotent.
    void enableHelloConnectivity();

    /// Native/test hook: suppress the FreeRTOS emit-task spawn so the caller
    /// drives emitHello() and the per-jack link machines (via sync()) on one thread.
    void setExternalConnectivityTask(bool external) { externalConnectivityTask = external; }

    /// The connectivity task's sole action: emit one HELLO frame on every jack.
    /// TX only — touches no SM state and fires no callback, so it is safe to run
    /// on a separate FreeRTOS task while the main loop owns everything else.
    void emitHello();

#ifndef NATIVE_BUILD
    /// FreeRTOS task body: emit at HELLO_CADENCE_MS until the destructor requests
    /// a stop, then self-delete. A member so it can read the stop flags directly.
    void connectivityTaskBody();
#endif

    /// #157 drives this once the context exchange completes: CONNECTING->CONNECTED
    /// and fires the jack-connect observer.
    void onContextExchangeComplete(SerialIdentifier jack);

    /// This jack's HELLO link state (observability / tests).
    HelloLinkState getHelloLinkState(SerialIdentifier jack) const;

    /// Registers the chain-role transition observer.
    void setOnChainRoleChange(ChainRoleChangeCallback callback) {
        chainRoleChangeCallback = std::move(callback);
    }

    /// Registers the head-only roster observer.
    void setOnMembershipChange(MembershipChangeCallback callback) {
        membershipChangeCallback = std::move(callback);
    }

    /// Registers the head-only ring-closure observer.
    void setOnRingClosed(RingClosedCallback callback) {
        ringClosedCallback = std::move(callback);
    }

    /**
     * Called when a chain announcement is received from a direct peer.
     * Replaces the port's daisy-chained peer list with the announced list,
     * filtering out self-MAC and the direct peer.
     */
    void onChainAnnouncementReceived(
        const uint8_t* fromMac,
        SerialIdentifier port,
        const std::vector<std::array<uint8_t, 6>>& announcedPeers);

    /// Registers the legacy any-chain-change observer.
    void setChainChangeCallback(std::function<void()> callback);

    /// Direct peer drops only; daisy-chained drops arrive via chain announcements.
    void setPeerLostCallback(std::function<void(const uint8_t*)> callback);

    /// Decodes and applies an inbound kChainAnnouncement packet.
    void processChainAnnouncementPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);

    using AnnouncementEmitCallback = std::function<void(const uint8_t* toMac, uint8_t announcementId, const std::vector<std::array<uint8_t, 6>>& peers)>;
    /// Registers the outbound chain-announcement sender.
    void setAnnouncementEmitCallback(AnnouncementEmitCallback callback);

    /// Decodes and applies an inbound kChainAnnouncementAck packet.
    void processChainAnnouncementAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);

    /// Registers the MAC as an ESP-NOW peer slot.
    void registerPeer(const uint8_t* macAddress);
    /// Releases the MAC's ESP-NOW peer slot.
    void unregisterPeer(const uint8_t* macAddress);

    // Retry / reliability observability. Cumulative since boot. For hardware
    // validation tuning of ackTimeoutMs_ and maxRetries_ against the real
    // deployment. ackLatencyMs / ackCount give mean RTT; abandons / (sends +
    // retries) gives loss rate at the chain-announcement layer.
    struct RetryStats {
        uint32_t sends = 0;
        uint32_t retries = 0;
        uint32_t abandons = 0;
        uint32_t ackLatencyMsSum = 0;
        uint32_t ackCount = 0;
    };
    /// Cumulative chain-announcement retry counters (see RetryStats).
    RetryStats getRetryStats() const { return retryStats_; }

private:
    RetryStats retryStats_;
    static constexpr size_t kNumPorts = 3;
    std::array<std::vector<std::array<uint8_t, 6>>, kNumPorts> daisyChainedByPort_;
    std::array<std::optional<std::array<uint8_t, 6>>, kNumPorts> previousDirectPeer_;
    uint8_t nextAnnouncementId_ = 1;

    struct PendingAnnouncement {
        bool active = false;
        uint8_t announcementId = 0;
        uint8_t retries = 0;
        std::vector<std::array<uint8_t, 6>> peers;
        SimpleTimer timer;
    };
    std::array<PendingAnnouncement, kNumPorts> pendingByPort_;
    static constexpr unsigned long ackTimeoutMs_ = 100;
    static constexpr uint8_t maxRetries_ = 3;
    // ESP-NOW peer-table capacity is 20 on ESP32-S3. Reserve margin for the
    // direct peer on each jack, the champion registration on supporters, and
    // brief transient registrations during chain reconfig.
    static constexpr size_t kMaxChainPeersPerPort = 18;

    void emitAnnouncementVia(SerialIdentifier viaPort, const std::vector<std::array<uint8_t, 6>>& peers);
    std::vector<std::array<uint8_t, 6>> peersReachableVia(SerialIdentifier port);

    size_t portIndex(SerialIdentifier port) const;

    void notifyDisconnect();
    void notifyConnect();
    void notifyDaisyChained();

    /**
     * handshake idle state - disconnected
     * handshake send id state - connecting
     * handshake connected state - connected
     */
    PortStatus mapHandshakeStateToStatus(SerialIdentifier port);

    void addDaisyChainedPeer(SerialIdentifier port, const uint8_t* macAddress);
    void removeDaisyChainedPeer(SerialIdentifier port, const uint8_t* macAddress);

    SerialManager* serialManager = nullptr;
    WirelessManager* wirelessManager_ = nullptr;
    std::function<void()> chainChangeCallback_;
    std::function<void(const uint8_t*)> peerLostCallback_;
    AnnouncementEmitCallback announcementEmitCallback_;

    // New-surface observers (#154); fired by the RDC internals as #155-#159 land.
    JackChangeCallback jackChangeCallback;
    ChainRoleChangeCallback chainRoleChangeCallback;
    MembershipChangeCallback membershipChangeCallback;
    RingClosedCallback ringClosedCallback;

    HandshakeWirelessManager handshakeWirelessManager;

    HandshakeApp* inputPortHandshake = nullptr;
    HandshakeApp* outputPortHandshake = nullptr;
    HandshakeApp* secondaryInputPortHandshake = nullptr;

    // Returns the list of ports that have active handshake apps.
    std::vector<SerialIdentifier> activePorts() const;
    HandshakeApp* handshakeAppForPort(SerialIdentifier port) const;

    // ---- HELLO connectivity internals (#155) ----
    struct JackHelloLink {
        SerialFrameParser parser;  // non-copyable; default-constructed in place
        HelloLinkMachine* machine = nullptr;  // per-jack link SM; owned, deleted in dtor
        // Recorded from the peer's received context; consumed by the chain SM (#156).
        uint8_t peerChainRole = 0;
        // Last recovery resend to this jack's peer (0 = never); throttles the
        // CONNECTED-state resend so two CONNECTED sides can't volley at radio RTT.
        unsigned long lastContextResendMs = 0;
    };
    std::array<JackHelloLink, kNumPorts> helloByPort;
    bool helloConnectivityEnabled = false;
    bool externalConnectivityTask = false;
    ContextReceivedCallback contextReceivedCallback;
    PlayerProfile selfPlayerProfile{};
    std::array<uint8_t, 6> selfMac{};
    DeviceType selfDeviceType = DeviceType::UNKNOWN;

    // Owned reliable transport + one context channel per device-type PktType.
    // Device-level, not per-jack: on receive the source MAC is matched back to a
    // jack. Constructed in initialize() once the WirelessManager is known.
    ReliableTransport* transport = nullptr;
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
        uint8_t chainRole = 0;
        std::array<uint8_t, sizeof(PlayerProfile)> profile{};
        size_t len = 0;
        unsigned long arrivedAtMs = 0;
        bool valid = false;
        bool isExpiredAt(unsigned long now) const {
            return now >= arrivedAtMs && now - arrivedAtMs > CONTEXT_BUFFER_TTL_MS;
        }
    };
    std::array<BufferedContext, CONTEXT_BUFFER_SLOTS> contextBuffer;

    // A jack entering Connecting initiates: apply any context buffered for its peer,
    // register the peer as a radio slot, then reliably send this device's context
    // (skipped if our other jack already has a send pending to that same peer).
    void initiateContextExchange(SerialIdentifier jack);
    // Serialize + reliably send this device's context to `mac` per selfDeviceType.
    void sendSelfContext(const uint8_t* mac);
    // Cache a received peer context by MAC, then apply it to jacks (rationale in .cpp).
    void onContextReceived(const uint8_t* fromMac, DeviceType peerType,
                           uint8_t chainRole, const uint8_t* profile, size_t len);
    // Completes every jack currently CONNECTING to `fromMac` (live receive path).
    void applyContextToJacks(const uint8_t* fromMac, DeviceType peerType,
                             uint8_t chainRole, const uint8_t* profile, size_t len);
    // Assumes `jack` is CONNECTING to this peer; both timing paths route through here,
    // so the context callback fires exactly once per jack.
    void completeJackContext(SerialIdentifier jack, DeviceType peerType, uint8_t chainRole,
                             const uint8_t* profile, size_t len);
    // Holds a context whose jack is not yet CONNECTING; evicts oldest when full.
    void bufferContext(const uint8_t* fromMac, DeviceType peerType, uint8_t chainRole,
                       const uint8_t* profile, size_t len);
    // Applies any cached context for `jack`'s peer to `jack` as it connects. Leaves
    // the cache entry for the peer's other jack (2-node ring); the TTL clears it.
    void drainBufferedContext(SerialIdentifier jack, const uint8_t* mac);
    // Link death on `jack`: release the peer's radio slot unless another jack or a
    // daisy chain still references the MAC (2-node ring keeps the slot).
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

    HWSerialWrapper* jackWrapper(SerialIdentifier port) const;
    bool isHelloJack(SerialIdentifier port) const;
    void onHelloReceived(SerialIdentifier jack, const HelloPayload& hello);
    PortStatus mapHelloLinkToStatus(SerialIdentifier port) const;
    static unsigned long nowMs();

    // ---- Device-level chain state machine (#156) ----
    // headMac (48 bits) + a confirmed bit packed into one atomic: the emit task
    // reads it while the main loop writes it. confirmed is wired but always 0
    // until #157's context exchange populates it.
    static constexpr uint64_t HEAD_MAC_MASK = 0xFFFFFFFFFFFFULL;
    static constexpr uint64_t CONFIRMED_BIT = 1ULL << 48;
    std::atomic<uint64_t> chainHeadState{0};
    // Latched when this structural head sees its own MAC return on the INPUT jack;
    // cleared when any ring link drops. Dominates getChainRole().
    bool ringLatched = false;
    // Last time the own-MAC head returned on INPUT while latched (seeded at
    // closure): the ring-latch evidence stamp checked against RING_EVIDENCE_TIMEOUT_MS.
    unsigned long lastSelfHeadReturnMs = 0;
    // Stable storage backing getHeadMac()'s returned pointer.
    mutable std::array<uint8_t, 6> headMacScratch{};
    // Last role reported to chainRoleChangeCallback, for edge-triggered firing.
    ChainRole lastChainRole = ChainRole::STANDALONE;

    static uint64_t packHead(const uint8_t* mac, bool confirmed);
    static void unpackMac(uint64_t value, uint8_t* out);
    void applyUpstreamHead(const HelloPayload& hello);
    void onLinkLost(SerialIdentifier port);
    void maybeFireChainRoleChange();
    // Drops a former head's radio slot (and its dead roster retries) unless an
    // adjacent link or daisy record still uses the MAC.
    void releaseHeadPeer(uint64_t headMac48);

    // ---- Head roster (#158) ----
    // member MAC -> its direct upstream MAC. Only the roster authority (see
    // isRosterAuthority) accepts roster traffic or serves getChainMembers.
    std::map<std::array<uint8_t, 6>, std::array<uint8_t, 6>> chainRoster;
    // seqId of the announce most recently sent; delivery of any earlier one is
    // stale and must not raise the confirmed bit.
    uint8_t pendingAnnounceSeqId = 0;
    // disconnectedMac of the report most recently sent whose delivery is not yet
    // confirmed (all-zero = none). A head change cancels the in-flight report to
    // the old head, and unlike the announce path nothing re-triggers it, so
    // applyUpstreamHead re-sends it to the successor — otherwise the departed
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
    // Drop `mac` and every member whose upstream chain passes through it.
    void removeChainMemberSubtree(const uint8_t* mac);
    // One OUTPUT jack means one downstream: when `member` announces `upstream`,
    // any OTHER member still recorded on that upstream is stale — remove it
    // and its subtree before the new claim lands.
    void evictStaleUpstreamClaimant(const std::array<uint8_t, 6>& member,
                                    const std::array<uint8_t, 6>& upstream);
    // True when some member other than `member` already holds `upstream`. The
    // transfer path queries this to skip (not evict) a colliding insert.
    bool upstreamHeldByOther(const std::array<uint8_t, 6>& upstream,
                             const std::array<uint8_t, 6>& member) const;
    // Demotion handoff: unicast the roster to the successor head, then clear it.
    void transferRosterTo(const uint8_t* newHeadMac);
    void onConnectionAnnounce(const uint8_t* fromMac, const ConnectionAnnouncePayload& announce);
    void onDisconnectReport(const uint8_t* fromMac, const DisconnectReportPayload& report);
    void onHeadTransfer(const uint8_t* fromMac, const HeadTransferPayload& transfer);
};
