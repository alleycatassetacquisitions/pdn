#include "device/remote-device-coordinator.hpp"
#include "device/peer-graph-codec.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/serial-manager.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/direct-peer-table.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

#define TAG "RDC"

RemoteDeviceCoordinator::RemoteDeviceCoordinator() : directPeerTable_(DirectPeerTable()) {
    lastHelloRxMs_[0] = 0;
    lastHelloRxMs_[1] = 0;
}

RemoteDeviceCoordinator::~RemoteDeviceCoordinator() = default;


void RemoteDeviceCoordinator::initialize(WirelessManager* wirelessManager, SerialManager* serialManager, Device* PDN) {
    this->serialManager = serialManager;
    this->wirelessManager_ = wirelessManager;
    if (PDN != nullptr) selfDeviceType_ = PDN->getDeviceType();

    directPeerTable_.initialize(wirelessManager);
    directPeerTable_.setPeerChangeCallback(
        [this](SerialIdentifier jack, std::optional<DirectPeer> prev,
               std::optional<DirectPeer> cur) {
            onPeerTableChange(jack, std::move(prev), std::move(cur));
        });

    if (wirelessManager_ != nullptr) {
        const uint8_t* mac = wirelessManager_->getMacAddress();
        if (mac != nullptr) {
            net::Mac selfMac;
            std::copy_n(mac, 6, selfMac.begin());
            peerGraph_.setSelfMac(selfMac);
        } else {
            // A zero self-MAC makes every all-zero (open-jack) peer compare equal
            // to self, which corrupts edge/loop derivation. The radio must be up
            // before RDC initializes; surface the misordering loudly rather than
            // silently flooding source={0} beacons.
            LOG_E(TAG, "initialize: getMacAddress() null; peer-graph self-MAC unset");
        }
    }

    for (SerialIdentifier jack :
         {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        SerialFrameParser& parser = frameParsers_[portIndex(jack)];
        parser.setBinaryFrameHandler(
            [this, jack](const Frame& f) { ingestSerial(jack, f); });
        // feed() runs on the UART event task.
        if (serialManager != nullptr) {
            serialManager->setOnBytesReceivedCallback(
                [&parser](const uint8_t* data, size_t len) {
                    parser.feed(data, len);
                },
                jack);
        }
    }
}

void RemoteDeviceCoordinator::ingestSerial(SerialIdentifier jack,
                                          const Frame& frame) {
    using namespace net;
    // Receive path: validate, drop self, stamp liveness, enqueue. No graph or
    // macPeer mutation here; exec() does that single-threaded on the
    // main loop (see DeferredPacket). The parser delivers frames only after CRC.
    const unsigned long activityNow = nowMs();

    if (frame.opcode == peer_graph::kOpHello) {
        // HELLO: source is our direct peer on this jack; also the silent-link
        // liveness signal.
        auto hello = peer_graph::decodeHello(frame.payload);
        if (!hello || !isValidPeerMac(hello->source)) return;
        // Reject self-sourced HELLOs BEFORE the liveness stamp. The OUTPUT jack
        // can echo our own HELLO; counting that echo as liveness keeps the
        // silent-link from ever firing on a yanked OUTPUT cable.
        if (hello->source == peerGraph_.getSelfMac()) return;
        lastHelloRxMs_[portIndex(jack)] = activityNow;
        DeferredPacket ev;
        ev.kind = DeferredPacket::HELLO;
        ev.jack = jack;
        ev.mac = hello->source;
        ev.deviceType = static_cast<DeviceType>(hello->deviceType);
        ev.userId = hello->userId;
        enqueueRecv(ev);
        return;
    }

    if (frame.opcode == peer_graph::kOpBeacon) {
        // BEACON: a flooded topology frame.
        auto beacon = peer_graph::decodeBeacon(frame.payload);
        if (!beacon) return;
        // Source poisoning (all-zero/broadcast) is rejected by acceptBeacon, the
        // layer that owns the graph cache; exec() forwards only what it accepts.
        // Our own beacon came back around the ring: drop without forwarding.
        if (beacon->source == peerGraph_.getSelfMac()) return;
        DeferredPacket ev;
        ev.kind = DeferredPacket::BEACON;
        ev.jack = jack;
        ev.beacon = *beacon;
        enqueueRecv(ev);
        return;
    }
}

void RemoteDeviceCoordinator::exec() {
    using namespace net;
    // Swap the batch out under the lock, then process it lock-free; the
    // handlers (setMacPeer/ESP-NOW registration, BEACON flood, declareJackDead)
    // are slow and must not run while the receive callback waits to enqueue.
    // Identical to EspNowDriver::exec().
    std::queue<DeferredPacket> batch;
    {
        std::lock_guard<std::mutex> lk(recvMutex_);
        std::swap(batch, recvQueue_);
    }
    const unsigned long now = nowMs();
    while (!batch.empty()) {
        DeferredPacket ev = batch.front();
        batch.pop();
        if (ev.kind == DeferredPacket::HELLO) {
            const DirectPeer* cur = directPeerTable_.getMacPeer(ev.jack);
            const bool newPeer = (cur == nullptr || cur->macAddr != ev.mac);
            if (newPeer) {
                LOG_D(TAG, "HELLO jack=%d from=%02X%02X%02X (was %s)",
                      (int)portIndex(ev.jack), ev.mac[3], ev.mac[4], ev.mac[5],
                      cur ? "set" : "empty");
            }
            DirectPeer peer;
            peer.macAddr = ev.mac;
            peer.sid = ev.jack;
            peer.deviceType = ev.deviceType;
            peer.userId = ev.userId;
            // setMacPeer handles ESP-NOW registration; reconcileSelfPeers()
            // later in sync() reflects the change into the peer-graph + BEACON.
            directPeerTable_.setMacPeer(ev.jack, peer);
            // Catch up the freshly-connected neighbor: the change-gated flood
            // never re-delivers unchanged far beacons to a peer that joined or
            // rebooted after the ring converged, so replay our cache to it
            // once, via the paced queue (see kReplayPacingMs), not as a burst.
            if (newPeer) {
                auto& q = replayQueue_[portIndex(ev.jack)];
                q.clear();
                for (const auto& b : peerGraph_.allBeacons()) {
                    q.push_back(b.source);
                }
            }
        } else if (ev.kind == DeferredPacket::BEACON) {
            // Cache; if the content changed, flood onward on the opposite jack.
            if (peerGraph_.acceptBeacon(ev.beacon, now)) {
                auto forward = peer_graph::encodeBeacon(ev.beacon);
                emitFrame(oppositeJack(ev.jack), forward);
            }
        } else if (ev.kind == DeferredPacket::JACK_SILENT) {
            // The watchdog (serviceConnectivity) flagged this jack dead. Do the
            // macPeer/parser teardown here so it stays single-threaded.
            declareJackDead(ev.jack);
        }
    }
}

void RemoteDeviceCoordinator::enqueueRecv(const DeferredPacket& ev) {
    std::lock_guard<std::mutex> lk(recvMutex_);
    if (recvQueue_.size() >= kRecvQueueMax) recvQueue_.pop();
    recvQueue_.push(ev);
}

void RemoteDeviceCoordinator::emitFrame(SerialIdentifier jack,
                                       const std::vector<uint8_t>& frame) {
    if (!serialManager) return;
    serialManager->writeBytes(frame.data(), frame.size(), jack);
    stats_[portIndex(jack)].framesTx++;
}

unsigned long RemoteDeviceCoordinator::nowMs() const {
    // No clock -> 0 (treat as "just started", never fire the silent-link
    // watchdog). This differs deliberately from SerialFrameParser::nowMs,
    // which returns max() to force a timeout and surface a clock misconfig;
    // here a false silent-death would tear down live links, so 0 is safer.
    auto* clk = SimpleTimer::getPlatformClock();
    return clk ? clk->milliseconds() : 0;
}

void RemoteDeviceCoordinator::serviceConnectivity(unsigned long now) {
    // (1) Emit HELLO on both jacks at a fixed cadence: the liveness signal our
    // neighbors watch. Always-on (gating on cable status deadlocks bootstrap).
    // Writes directly via the serial manager, NOT emitFrame, so the steady HELLO
    // stream doesn't inflate the framesTx diagnostic counter.
    if (serialManager != nullptr && now - lastHelloEmitMs_ >= kHelloEmitMs) {
        lastHelloEmitMs_ = now;
        const uint16_t selfUserId =
            userIdProvider_ ? userIdProvider_() : peer_graph::kUserIdUnknown;
        // HELLO content only changes when the user id does (MAC and device type
        // are fixed), so re-encode on change instead of heap-building the same
        // 14 bytes 50 times a second on this real-time task.
        if (cachedHelloFrame_.empty() || selfUserId != cachedHelloUserId_) {
            cachedHelloFrame_ = peer_graph::encodeHello(
                peerGraph_.getSelfMac(), static_cast<uint8_t>(selfDeviceType_),
                selfUserId);
            cachedHelloUserId_ = selfUserId;
        }
        serialManager->writeBytes(cachedHelloFrame_.data(), cachedHelloFrame_.size(),
                                  SerialIdentifier::INPUT_JACK);
        serialManager->writeBytes(cachedHelloFrame_.data(), cachedHelloFrame_.size(),
                                  SerialIdentifier::OUTPUT_JACK);
    }

    // (2) Silent-link watchdog. Gates on the atomic lastHelloRxMs only (no
    // peer-table read), so it is race-free when run from a dedicated task:
    // a jack whose HELLO baseline has gone stale is dead. baseline is 0 until
    // the first HELLO and reset to 0 on death, so a never-connected jack never
    // fires. Enqueue JACK_SILENT; exec() does the declareJackDead so all
    // macPeer/parser teardown stays single-threaded.
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const uint8_t idx = portIndex(port);
        const unsigned long baseline = lastHelloRxMs_[idx];
        // baseline can briefly exceed `now`: the RX path stamps lastHelloRxMs_
        // from another task and may land between the `now` capture at the top of
        // serviceConnectivity and this read. A HELLO that recent means the link
        // is alive, so clamp to gap 0 instead of letting unsigned subtraction
        // underflow to ~UINT32_MAX and fire a false silent-death. This form
        // trades exactness across the 32-bit millis wrap (~49.7 days) for
        // race-safety; device uptime at an event is hours, so the wrap boundary
        // is unreachable.
        const unsigned long gap = baseline >= now ? 0 : now - baseline;
        const bool silentLinkExpired =
            baseline != 0 && gap > helloSilentLinkMs_;
        if (silentLinkExpired) {
            LOG_D(TAG, "JACKDEAD jack=%d reason=silent gap=%lu", (int)idx, gap);
            DeferredPacket ev;
            ev.kind = DeferredPacket::JACK_SILENT;
            ev.jack = port;
            enqueueRecv(ev);
        }
    }
    // The 1Hz BEACON backstop stays on the main loop (sync), not here: it reads
    // lastEmitted{In,Out}Peer_, which reconcileSelfPeers writes on the main loop.
}

void RemoteDeviceCoordinator::sync() {
    const unsigned long now = nowMs();

    // Connectivity producer: emit HELLO + run the watchdog. On hardware a 20ms
    // task owns this (externalConnectivityTask_ set), so sync() skips it; on
    // native it runs inline so the tests drive identical logic via sync().
    if (!externalConnectivityTask_) serviceConnectivity(now);

    // Drain the receive queue: RX HELLO/BEACON (macPeer, accept+flood) and the
    // watchdog's JACK_SILENT (declareJackDead), all single-threaded, before
    // the direct-peer scan and reconcile read macPeer.
    exec();

    // Direct-peer connect/disconnect edges fire inline from set/removeMacPeer
    // (see onPeerTableChange), driven by exec() above; no separate poll.

    // Runs last so connect/disconnect/jack-dead mutations from this tick are
    // captured.
    reconcileSelfPeers();

    // The role byte is BEACON content like the peer slots: re-emit on a change
    // rather than waiting for the backstop. emitBeaconBothJacks() is also what
    // copies the byte into the local graph (setSelfRole), so without this the
    // local champion walk reads a stale role for up to a backstop period.
    const uint8_t roleNow = roleProvider_ ? roleProvider_() : 0;
    if (roleNow != lastEmittedRole_) {
        emitBeaconBothJacks();
    }

    // Periodic BEACON backstop (1Hz). Main-loop only: it reads the self-peers
    // reconcileSelfPeers just wrote. Closes the cold-boot convergence gap; the
    // content-dedup at receivers stops it from flooding when unchanged.
    if (now - lastBeaconBackstopMs_ >= kBeaconBackstopMs) {
        lastBeaconBackstopMs_ = now;
        if (lastEmittedInPeer_ != net::Mac{} ||
            lastEmittedOutPeer_ != net::Mac{}) {
            emitBeaconBothJacks();
            replayChainBeacons();
        }
    }

    // Trickle queued replay frames out under the pacing cap. Runs after the
    // backstop so a fresh refill starts draining the same tick.
    drainReplayQueues(now);
}

void RemoteDeviceCoordinator::reconcileSelfPeers() {
    net::Mac inPeer{};
    net::Mac outPeer{};
    if (const DirectPeer* p = directPeerTable_.getMacPeer(SerialIdentifier::INPUT_JACK)) {
        inPeer = p->macAddr;
    }
    if (const DirectPeer* p = directPeerTable_.getMacPeer(SerialIdentifier::OUTPUT_JACK)) {
        outPeer = p->macAddr;
    }
    if (inPeer == lastEmittedInPeer_ && outPeer == lastEmittedOutPeer_) return;
    // Logs only the low 3 MAC bytes; two peers sharing those bytes would print
    // identically. Adequate for the 6th-byte-unique test scheme; widen if a
    // real collision ever needs disambiguation on hardware.
    LOG_D(TAG, "SELFPEERS in=%02X%02X%02X out=%02X%02X%02X",
          inPeer[3], inPeer[4], inPeer[5], outPeer[3], outPeer[4], outPeer[5]);
    lastEmittedInPeer_ = inPeer;
    lastEmittedOutPeer_ = outPeer;
    peerGraph_.setSelfPeers(inPeer, outPeer, nowMs());
    emitBeaconBothJacks();
}

void RemoteDeviceCoordinator::emitBeaconBothJacks() {
    BeaconRecord beacon;
    beacon.source = peerGraph_.getSelfMac();
    beacon.inPeer = lastEmittedInPeer_;
    beacon.outPeer = lastEmittedOutPeer_;
    const uint8_t selfRole = roleProvider_ ? roleProvider_() : 0;
    beacon.role = selfRole;
    // Keep the graph's self-node byte in lockstep with what we flood, so the
    // local champion walk and remote peers read the same role.
    peerGraph_.setSelfRole(selfRole);
    lastEmittedRole_ = selfRole;
    auto frame = peer_graph::encodeBeacon(beacon);
    emitFrame(SerialIdentifier::INPUT_JACK, frame);
    emitFrame(SerialIdentifier::OUTPUT_JACK, frame);
}

void RemoteDeviceCoordinator::replayChainBeacons() {
    // A forwarded BEACON lost in transit is never re-delivered by the
    // change-gated flood: nodes past the loss keep stale topology until the
    // source itself changes. Replaying cached beacons heals it; receivers
    // dedup unchanged content. Chain members only; departed nodes' beacons
    // stay cached and replaying them spends wire on zombies all event. Refill
    // only an empty queue, and only peered jacks.
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        auto& q = replayQueue_[portIndex(port)];
        if (!q.empty()) continue;
        if (directPeerTable_.getMacPeer(port) == nullptr) continue;
        for (const auto& member : peerGraph_.getChainMembers()) {
            if (member == peerGraph_.getSelfMac()) continue;
            q.push_back(member);
        }
    }
}

void RemoteDeviceCoordinator::drainReplayQueues(unsigned long now) {
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const uint8_t idx = portIndex(port);
        auto& q = replayQueue_[idx];
        if (q.empty()) continue;
        // Clamp like the silent-link gap: a backwards clock must pace, not
        // burst.
        const unsigned long gap =
            lastReplayEmitMs_[idx] >= now ? 0 : now - lastReplayEmitMs_[idx];
        if (lastReplayEmitMs_[idx] != 0 && gap < kReplayPacingMs) continue;
        // Pop until a source that still has a cached beacon; skipping vanished
        // sources is cheaper than assuming the cache is never pruned.
        while (!q.empty()) {
            const net::Mac src = q.front();
            q.pop_front();
            if (auto cached = peerGraph_.getBeacon(src)) {
                emitFrame(port, peer_graph::encodeBeacon(*cached));
                lastReplayEmitMs_[idx] = now;
                break;
            }
        }
    }
}

uint8_t RemoteDeviceCoordinator::portIndex(SerialIdentifier port) const {
    return port == SerialIdentifier::INPUT_JACK ? 0 : 1;
}

PortStatus RemoteDeviceCoordinator::getPortStatus(SerialIdentifier port) {
    // Connectivity is direct-peer presence: macPeer is set in exec() when a
    // HELLO arrives on the jack and cleared by declareJackDead on disconnect.
    return directPeerTable_.getMacPeer(port) != nullptr
        ? PortStatus::CONNECTED
        : PortStatus::DISCONNECTED;
}

void RemoteDeviceCoordinator::setOnDirectPeerChange(DirectPeerChangeCallback cb) {
    onDirectPeerChange_ = std::move(cb);
}

void RemoteDeviceCoordinator::fireDirectPeerChange(SerialIdentifier port,
                                                   std::optional<Peer> previous,
                                                   std::optional<Peer> current) {
    if (onDirectPeerChange_) onDirectPeerChange_(port, previous, current);
}

DeviceType RemoteDeviceCoordinator::getPeerDeviceType(SerialIdentifier port) const {
    const DirectPeer* macPeer = directPeerTable_.getMacPeer(port);
    return macPeer ? macPeer->deviceType : DeviceType::UNKNOWN;
}

bool RemoteDeviceCoordinator::isInLoop() const {
    if (peerGraph_.isInLoop()) return true;
    // Two-device ring: both jacks lead to the same peer. The peer-graph needs two
    // distinct neighbors to see a cycle, so detect this 2-cycle from the direct
    // peers.
    const uint8_t* in = getPeerMac(SerialIdentifier::INPUT_JACK);
    const uint8_t* out = getPeerMac(SerialIdentifier::OUTPUT_JACK);
    return in != nullptr && out != nullptr && std::memcmp(in, out, 6) == 0;
}

std::optional<uint16_t> RemoteDeviceCoordinator::getPeerUserId(SerialIdentifier port) const {
    const DirectPeer* macPeer = directPeerTable_.getMacPeer(port);
    if (macPeer == nullptr || macPeer->userId == peer_graph::kUserIdUnknown) {
        return std::nullopt;
    }
    return macPeer->userId;
}

const uint8_t* RemoteDeviceCoordinator::getPeerMac(SerialIdentifier port) const {
    const DirectPeer* peer = directPeerTable_.getMacPeer(port);
    return peer ? peer->macAddr.data() : nullptr;
}

bool RemoteDeviceCoordinator::isDirectPeer(const uint8_t* mac) const {
    if (!mac) return false;
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const uint8_t* peer = getPeerMac(port);
        if (peer && memcmp(peer, mac, 6) == 0) return true;
    }
    return false;
}

void RemoteDeviceCoordinator::registerRadioPeer(const uint8_t* macAddress) {
    if (wirelessManager_ != nullptr) {
        wirelessManager_->addEspNowPeer(macAddress);
    }
}

void RemoteDeviceCoordinator::declareJackDead(SerialIdentifier jack) {
    // Clear the direct-peer slot. removeMacPeer fires onPeerTableChange,
    // which delivers the synthetic disconnect to game-layer subscribers,
    // releases the ESP-NOW slot, and resets the silent-link baseline.
    // reconcileSelfPeers() on the next sync() picks up the now-cleared macPeer,
    // drops the edge from the peer-graph, and broadcasts the updated BEACON.
    directPeerTable_.removeMacPeer(jack);
    // A dead jack has no listener; drop any replay still queued for it.
    replayQueue_[portIndex(jack)].clear();
    // Defer the parser reset to the UART event task: clearing its buffers
    // synchronously would race feed().
    frameParsers_[portIndex(jack)].requestReset();
}

void RemoteDeviceCoordinator::onPeerTableChange(
        SerialIdentifier jack,
        std::optional<DirectPeer> previous,
        std::optional<DirectPeer> current) {
    const uint8_t idx = portIndex(jack);
    if (current.has_value()) {
        // Connect: register the radio peer, seed the silent-link baseline so the
        // watchdog doesn't fire before the next HELLO, and notify subscribers.
        registerRadioPeer(current->macAddr.data());
        lastHelloRxMs_[idx] = nowMs();
        fireDirectPeerChange(jack, std::nullopt,
                             Peer{current->macAddr, current->deviceType});
        return;
    }
    if (previous.has_value()) {
        // Disconnect: notify subscribers (now with the dropped peer's real
        // deviceType), then release the ESP-NOW slot unless the same neighbour is
        // still wired to the other jack (a 2-device loop on both jacks). The
        // 20-slot table is finite; leaking one entry per distinct neighbour that
        // silent-dies over a multi-hour event eventually rejects new peers.
        // macPeer for this jack is already cleared, so isDirectPeer() now only
        // sees the other jack. Finally reset the baseline for the next connect.
        fireDirectPeerChange(jack,
                             Peer{previous->macAddr, previous->deviceType},
                             std::nullopt);
        if (!isDirectPeer(previous->macAddr.data())) {
            unregisterRadioPeer(previous->macAddr.data());
        }
        lastHelloRxMs_[idx] = 0;
    }
}

void RemoteDeviceCoordinator::unregisterRadioPeer(const uint8_t* macAddress) {
    if (wirelessManager_ != nullptr) {
        wirelessManager_->removeEspNowPeer(macAddress);
    }
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const DirectPeer* peer = directPeerTable_.getMacPeer(port);
        if (peer != nullptr && memcmp(peer->macAddr.data(), macAddress, 6) == 0) {
            directPeerTable_.removeMacPeer(port);
        }
    }
}

