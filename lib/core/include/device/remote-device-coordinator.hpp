#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include "device/serial-frame-parser.hpp"
#include "device/serial-manager.hpp"
#include "device/mac-types.hpp"
#include "device/peer-graph.hpp"
#include "wireless/direct-peer-table.hpp"
#include "device/device-type.hpp"

class Device;

enum class PortStatus {
    DISCONNECTED, // No direct peer on this jack (macPeer slot empty).
    CONNECTED,    // A direct peer is present on this jack.
};

// Owns physical jack connectivity, independent of game state: who is plugged
// into each jack (HELLO), and the chain/ring topology (BEACON flood into the
// peer graph). Game meaning (roles, champion) is injected by the game layer;
// RDC transports bytes it never interprets.
//
// Serial frame lifecycle:
//   out: serviceConnectivity emits HELLO every kHelloEmitMs per jack (on
//     hardware: a dedicated task, so liveness survives main-loop stalls);
//     BEACONs emit on self-edge/role change, on the 1Hz backstop, and via the
//     paced replay queues (connect catch-up + mid-chain loss repair).
//   in: UART event task feeds SerialFrameParser -> ingestSerial validates,
//     stamps HELLO liveness, and enqueues a DeferredPacket (drop-oldest cap).
//     No graph or peer mutation off the main loop: exec() drains the queue,
//     mutates macPeers/graph, and forwards changed BEACONs out the opposite
//     jack; that hop-by-hop relay is the flood.
// The flood is change-gated, so every loss class has a periodic healer (see
// the backstop comments below); they are load-bearing, not redundancy.
class RemoteDeviceCoordinator {
public:
    // Slimmer than DirectPeer (direct-peer-table.hpp): carries only what
    // change subscribers need.
    struct Peer {
        std::array<uint8_t, 6> mac;
        DeviceType deviceType;
    };

    // previous = std::nullopt on connect; current = std::nullopt on disconnect.
    // Both populated never happens; RDC fires only on presence transitions.
    using DirectPeerChangeCallback =
        std::function<void(SerialIdentifier port,
                           std::optional<Peer> previous,
                           std::optional<Peer> current)>;

    RemoteDeviceCoordinator();
    ~RemoteDeviceCoordinator();

    // Wires the per-jack parsers to ingestSerial and seeds the peer graph
    // with the self MAC. The radio must be up first (a null self MAC corrupts
    // edge derivation; initialize logs loudly if it is).
    void initialize(WirelessManager* wirelessManager,
                    SerialManager* serialManager,
                    Device* PDN);

    // Must be called every loop tick (from PDN::loop): drains the deferred
    // receive queue (exec), reconciles self peers into the graph, and runs the
    // BEACON backstop + paced replay.
    void sync();

    virtual PortStatus getPortStatus(SerialIdentifier port);

    // Returns a pointer to the peer's MAC address for the given port, or nullptr
    // if no peer is connected.
    virtual const uint8_t* getPeerMac(SerialIdentifier port) const;

    virtual DeviceType getPeerDeviceType(SerialIdentifier port) const;

    // The direct peer's 4-digit player id, learned from its HELLO. nullopt if no
    // peer or the peer has not registered an id yet (0xFFFF on the wire).
    virtual std::optional<uint16_t> getPeerUserId(SerialIdentifier port) const;

    // Source for this device's own 4-digit id, packed 0-9999 (0xFFFF if not yet
    // registered), sampled each HELLO emit. Set by the game layer so RDC stays
    // decoupled from Player.
    void setUserIdProvider(std::function<uint16_t()> provider) {
        userIdProvider_ = std::move(provider);
    }

    // Source for this device's opaque game byte, flooded verbatim in every
    // BEACON. RDC never interprets it (encoding lives in the game layer, see
    // chain_role::Role); a device with no provider floods 0, which the game
    // layer reads as "no role" (FDNs never install a provider).
    void setRoleProvider(std::function<uint8_t()> provider) {
        roleProvider_ = std::move(provider);
    }

    // Game-layer interpreters over the peer graph, injected by ChainManager:
    // RDC owns the graph but not the meaning of its role byte. The virtual
    // getters below stay the single read path so tests keep one seam to stub.
    using ChampionResolver =
        std::function<std::optional<net::Mac>(const PeerGraph&)>;
    using OpponentRoleResolver =
        std::function<std::optional<bool>(const PeerGraph&)>;
    void setChampionResolver(ChampionResolver resolver) {
        championResolver_ = std::move(resolver);
    }
    void setOpponentRoleResolver(OpponentRoleResolver resolver) {
        opponentRoleResolver_ = std::move(resolver);
    }

    // Returns true iff `mac` matches the direct peer on either jack.
    virtual bool isDirectPeer(const uint8_t* mac) const;

    // Fires on direct-peer presence transitions on each jack:
    //   connect:    previous = nullopt, current = Peer{newMac, type}
    //   disconnect: previous = Peer{oldMac, UNKNOWN}, current = nullopt
    // The disconnect Peer carries the just-dropped MAC so subscribers can take
    // MAC-specific action (Shootout PEER_LOST, etc.). DeviceType is UNKNOWN
    // because RDC doesn't preserve it across the drop.
    void setOnDirectPeerChange(DirectPeerChangeCallback cb);

    // ESP-NOW radio peer-table slot management ("radio" to distinguish from
    // player registration, the userId sense of "registered" above).
    // unregisterRadioPeer also clears any direct-peer slot still naming the
    // MAC, a no-op on the internal disconnect path (already gated on
    // !isDirectPeer) but load-bearing for direct callers.
    void registerRadioPeer(const uint8_t* macAddress);
    void unregisterRadioPeer(const uint8_t* macAddress);

    // Connected component of self under the peer-graph's mutual edges. Order is
    // implementation-defined (callers must treat as a set). Virtual so test
    // fixtures can subclass RDC and inject deterministic snapshots.
    virtual std::vector<net::Mac> getChainMembers() const { return peerGraph_.getChainMembers(); }

    // True iff self is in a cycle: either the peer-graph finds a ring of mutual
    // edges, or both jacks lead to the same direct peer (a two-device ring, which
    // the graph can't represent as a 2-cycle). Virtual for test override.
    virtual bool isInLoop() const;

    // Head of self's own-role chain, derived from the BEACON flood by the
    // injected resolver (see chain_role::championMac). nullopt inside a loop,
    // while unresolved, or before a resolver is installed. Virtual so test
    // fixtures can inject a champion without a full topology.
    virtual std::optional<net::Mac> getChampionMac() const {
        return championResolver_ ? championResolver_(peerGraph_) : std::nullopt;
    }

    // Hunter/bounty role of self's opponent-jack peer when it forms a mutual
    // edge, via the injected resolver (see chain_role::mutualOpponentIsHunter).
    // Virtual for test override.
    virtual std::optional<bool> mutualOpponentIsHunter() const {
        return opponentRoleResolver_ ? opponentRoleResolver_(peerGraph_)
                                     : std::nullopt;
    }

    // Run serviceConnectivity (HELLO emit + silent-link watchdog) on a fixed
    // cadence; call this from a hardware timer/task. While set, sync() stops
    // running serviceConnectivity itself (the task owns it). Native leaves it
    // off, so sync() drives everything single-threaded for the tests.
    void serviceConnectivity(unsigned long now);
    void setExternalConnectivityTask(bool on) { externalConnectivityTask_ = on; }

    // Count of chain members reachable through the direct peer on `jack`,
    // excluding self, i.e. how many devices are chained behind us on that
    // side. Returns 0 if no direct peer is present on the jack. Virtual so
    // test fixtures can inject deterministic chain depths.
    virtual size_t countChainBehind(SerialIdentifier jack) const {
        const uint8_t* peer = getPeerMac(jack);
        if (peer == nullptr) return 0;
        net::Mac firstHop;
        std::copy_n(peer, 6, firstHop.begin());
        return peerGraph_.countReachableExcludingSelf(firstHop);
    }

    // Drain recvQueue_, applying graph/macPeer mutations and BEACON floods
    // single-threaded, the connectivity analog of EspNowDriver::exec().
    // Called at the top of sync() in production; test fixtures call it to
    // apply a delivered frame without a full sync().
    void exec();

    // True iff no mutual edge has been added or removed in the last 200ms.
    // Consumers gate state derivation on this so brief topology churn (cable
    // wiggle, single dropped HELLO) doesn't thrash the game layer.
    virtual bool isTopologyStable() const { return peerGraph_.isTopologyStable(nowMs()); }

    // Test hook: synchronously declare a specific jack dead, the same teardown
    // the silent-link watchdog runs on a real disconnect. Lets the multi-device
    // fixture simulate a cable yank on one named jack without waiting out the
    // silent-link window (which it pins to 60s to survive clock-stepping).
    void declareJackDeadForTest(SerialIdentifier jack) {
        declareJackDead(jack);
    }

    // Test hook to override the silent-link jack-dead threshold (production:
    // kHelloSilentLinkMs). A single-device unit test has no partner emitting
    // HELLOs, so the production threshold trips during any multi-second time
    // advance (e.g. primeTopologyStableAll's 8.8s prime window). Tests that prime
    // then exercise long-running behavior call this with a longer threshold.
    void setJackDeadSilentLinkMsForTest(unsigned long ms) {
        helloSilentLinkMs_ = ms;
    }

    // Per-jack frame-emit counter, read by tests via getBeaconStats to assert
    // BEACON cadence. uint32_t (not uint16_t) because at the BEACON frame rate a
    // 16-bit counter wraps in minutes, shorter than a LARP round; 32-bit is
    // effectively unbounded. Written only on the main loop (emitFrame), read only
    // single-threaded in native tests, so no synchronization is needed.
    struct BeaconStats {
        uint32_t framesTx = 0;
    };

    const BeaconStats& getBeaconStats(SerialIdentifier jack) const {
        return stats_[jack == SerialIdentifier::INPUT_JACK ? 0 : 1];
    }

    // Producers (UART event task, 20ms watchdog) enqueue whether or not the
    // main loop drains; a stalled loop would otherwise grow this until OOM.
    // Sized for the worst legitimate burst: a 50-node connect-time cache
    // replay plus HELLO traffic between two ticks.
    static constexpr size_t kRecvQueueMax = 128;

    // Test hook: current recvQueue_ depth, for asserting the cap.
    size_t recvQueueDepthForTest() {
        std::lock_guard<std::mutex> lk(recvMutex_);
        return recvQueue_.size();
    }

private:
    std::array<BeaconStats, 2> stats_;

    // Silent-link watchdog threshold = disconnect-detection latency. A CONNECTED
    // jack with no HELLO in this window is declared dead. Peers emit HELLO every
    // 20ms (connectivityTask in main.cpp) and RX is timestamped on the UART event
    // task within ~1ms of arrival, so the worst-case inter-HELLO gap is ~one emit
    // period plus scheduling slack. 100ms is 5 missed emits: fast enough to feel
    // instant, with margin against transient task slack. Overridable via
    // setJackDeadSilentLinkMsForTest.
    //
    // Liveness is protocol-level (frames observed) rather than electrical
    // (jack-sense GPIO) on purpose: the only question that matters is whether
    // frames can flow, and TRS contacts can be electrically mated while passing
    // nothing (partial insertion, oxidation). A GPIO-based disconnect detector
    // existed and was removed after failing multi-device hardware testing;
    // don't reintroduce one without bench re-verification.
    static constexpr unsigned long kHelloSilentLinkMs = 100;
    unsigned long helloSilentLinkMs_ = kHelloSilentLinkMs;

    // Surrender a jack: clear its direct-peer slot (removeMacPeer fires the
    // disconnect side effects via onPeerTableChange) and reset the
    // frame parser. reconcileSelfPeers() on the next sync() propagates the
    // now-cleared edge via an updated BEACON.
    void declareJackDead(SerialIdentifier jack);

    // Wired to DirectPeerTable's PeerChangeCallback: the single place
    // direct-peer connect/disconnect side effects live (ESP-NOW peer
    // registration, silent-link watchdog baseline, and the game-layer
    // onDirectPeerChange notification). Fired inline from set/removeMacPeer, so
    // macPeers already reflects the transition when this runs.
    void onPeerTableChange(SerialIdentifier jack,
                               std::optional<DirectPeer> previous,
                               std::optional<DirectPeer> current);

    unsigned long nowMs() const;

    // Single writeBytes site for binary frames: increments framesTx.
    void emitFrame(SerialIdentifier jack, const std::vector<uint8_t>& frame);

    uint8_t portIndex(SerialIdentifier port) const;

    static SerialIdentifier oppositeJack(SerialIdentifier j) {
        return j == SerialIdentifier::INPUT_JACK
            ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
    }


    SerialManager* serialManager = nullptr;
    WirelessManager* wirelessManager_ = nullptr;
    DirectPeerChangeCallback onDirectPeerChange_;

    void fireDirectPeerChange(SerialIdentifier port,
                              std::optional<Peer> previous,
                              std::optional<Peer> current);

    DirectPeerTable directPeerTable_;

    // Per-jack byte-stream parsers, fed on the UART event task via the serial
    // wrapper's bytes callback; resets are requested from the main loop on jack
    // teardown and honored by the feeding task.
    SerialFrameParser frameParsers_[2];

    PeerGraph peerGraph_;

    // A received serial frame, parsed but not yet applied. Same idiom (and
    // name) as EspNowDriver: ingestSerial only validates + enqueues; the main
    // loop drains via exec() and runs the handlers single-threaded, so the
    // peer-graph needs no lock.
    struct DeferredPacket {
        enum Kind { HELLO, BEACON, JACK_SILENT } kind;
        SerialIdentifier jack;
        net::Mac mac{};               // HELLO source
        DeviceType deviceType = DeviceType::UNKNOWN;
        uint16_t userId = 0xFFFF;     // HELLO source's 4-digit id; 0xFFFF unknown
        BeaconRecord beacon{};                 // BEACON payload
    };
    std::queue<DeferredPacket> recvQueue_;
    // Guards recvQueue_ across the receive callback / connectivity task /
    // main-loop drain boundary. Held only to push or to swap the batch out
    // (exec processes the batch lock-free); same idiom as EspNowDriver.
    std::mutex recvMutex_;

    // Sole producer-side entry: sheds the oldest entry at kRecvQueueMax.
    // Drop-oldest is safe because every packet kind regenerates periodically
    // (HELLO, 1Hz BEACON backstop, JACK_SILENT each watchdog pass).
    void enqueueRecv(const DeferredPacket& ev);

    // Validate + enqueue a received frame (no graph/macPeer mutation here).
    void ingestSerial(SerialIdentifier jack, const Frame& frame);

    // serviceConnectivity (HELLO emit + watchdog) is declared in the public
    // section so a hardware task can drive it. State it needs:
    unsigned long lastHelloEmitMs_ = 0;
    static constexpr unsigned long kHelloEmitMs = 20;
    bool externalConnectivityTask_ = false;        // set when a task owns it
    DeviceType selfDeviceType_ = DeviceType::PDN;  // cached at initialize()
    std::function<uint16_t()> userIdProvider_;     // self id source, set by game
    std::function<uint8_t()> roleProvider_;        // self game byte source, set by game
    ChampionResolver championResolver_;            // game-layer walk, injected
    OpponentRoleResolver opponentRoleResolver_;    // game-layer query, injected

    // HELLO frame cache: re-encoded only when the sampled user id changes
    // (empty = not yet built). Touched only inside serviceConnectivity, which
    // runs on exactly one task.
    std::vector<uint8_t> cachedHelloFrame_;
    uint16_t cachedHelloUserId_ = 0;

    // Per-jack timestamp of the last HELLO received: the liveness truth. Atomic
    // because the receive callback writes it while the watchdog (which may run on
    // a separate task) reads it. The silent-link watchdog declares a jack dead
    // when now - this exceeds kHelloSilentLinkMs. Initialised in the constructor.
    std::array<std::atomic<unsigned long>, 2> lastHelloRxMs_;

    // Last (in, out) self-peer MACs reflected into peerGraph_ and broadcast as
    // a BEACON. sync() reconciles the current macPeer slots against this; any
    // difference triggers a PeerGraph update + fresh BEACON emit on both jacks.
    net::Mac lastEmittedInPeer_{};
    net::Mac lastEmittedOutPeer_{};

    // Role byte carried by the last emitted BEACON. sync() re-emits when the
    // provider disagrees, so a role flip propagates immediately instead of on
    // the next backstop.
    uint8_t lastEmittedRole_ = 0;

    // Reconcile current macPeer slots into peerGraph_ and broadcast a BEACON
    // when they have changed since the last emit. Called once per sync().
    void reconcileSelfPeers();

    // Periodic BEACON re-emit cadence (convergence backstop, deduped by
    // receivers so it only propagates when it carries new information).
    unsigned long lastBeaconBackstopMs_ = 0;
    static constexpr unsigned long kBeaconBackstopMs = 1000;

    void emitBeaconBothJacks();

    // Refill each peered jack's replay queue with the current chain members
    // (1Hz, from the backstop, only when that jack's queue is empty). Heals
    // multi-hop BEACON loss on open chains, which the change-gated flood can
    // never repair on its own.
    void replayChainBeacons();

    // At most one replay frame per jack per pacing window: ~7 back-to-back
    // beacons on the 19200-baud jack stall the 20ms HELLO stream past the
    // 100ms silent-link threshold and the partner declares the jack dead.
    // Queues hold beacon SOURCES so the drain emits current cache content and
    // skips vanished entries. Main-loop only.
    static constexpr unsigned long kReplayPacingMs = 50;
    std::array<std::deque<net::Mac>, 2> replayQueue_;
    std::array<unsigned long, 2> lastReplayEmitMs_{0, 0};
    void drainReplayQueues(unsigned long now);
};
