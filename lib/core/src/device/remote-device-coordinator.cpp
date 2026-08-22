#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/serial-manager.hpp"
#include "device/drivers/logger.hpp"
#include "wireless/mac-functions.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/hello-link-machine.hpp"

RemoteDeviceCoordinator::RemoteDeviceCoordinator() = default;

#ifndef NATIVE_BUILD
// Free entry point xTaskCreate can take directly; the loop lives in a member so
// it can read the cooperative-stop flags.
static void connectivityTaskEntry(void* ctx) {
    static_cast<RemoteDeviceCoordinator*>(ctx)->connectivityTaskBody();
}

void RemoteDeviceCoordinator::connectivityTaskBody() {
    for (;;) {
        // Check before touching any state so a stop can never race an emit.
        if (connectivityTaskStopRequested.load()) {
            connectivityTaskExited.store(true);
            vTaskDelete(nullptr);  // self-delete; never returns
        }
        emitHello();
        vTaskDelay(pdMS_TO_TICKS(HELLO_CADENCE_MS));
    }
}
#endif

RemoteDeviceCoordinator::~RemoteDeviceCoordinator() {
#ifndef NATIVE_BUILD
    // Cooperatively stop the emit task before freeing anything it dereferences:
    // request exit, then wait for the task to observe it and self-delete. A
    // cross-core vTaskDelete could otherwise cut it mid-emit, stranding the UART
    // lock or dereferencing freed state.
    if (connectivityTaskHandle != nullptr) {
        connectivityTaskStopRequested.store(true);
        while (!connectivityTaskExited.load()) {
            vTaskDelay(pdMS_TO_TICKS(HELLO_CADENCE_MS));
        }
        connectivityTaskHandle = nullptr;
    }
#endif
    // Drop the byte callbacks: they capture `this` and live on the externally
    // owned jacks, so a post-destruction exec() would otherwise call a lambda
    // over freed memory.
    for (SerialIdentifier port : HELLO_JACKS) {
        HWSerialWrapper* hw = jackWrapper(port);
        if (hw != nullptr) hw->setByteCallback(nullptr);
        delete helloByPort[portIndex(port)].machine;
        helloByPort[portIndex(port)].machine = nullptr;
    }
    // Owns the channels; deletes them and their driver rx bindings.
    delete transport;
}


void RemoteDeviceCoordinator::initialize(WirelessManager* wirelessManager, SerialManager* serialManager, Device* PDN) {
    this->serialManager = serialManager;
    this->wirelessManager_ = wirelessManager;
    if (PDN != nullptr) {
        selfDeviceType = PDN->getDeviceType();
    }

    // One device-level channel per device-type PktType. Claiming a channel makes
    // its receive path live (the transport installs the driver rx handler). The
    // abandon callback is a no-op: an unreachable peer's jack link self-heals to
    // IDLE via the context-exchange timeout (HelloConnectingState, contextTimeoutMs).
    // The packet's PktType alone picks which channel decodes a context; the
    // HELLO deviceType is not consulted. RDC forwards the profile bytes opaquely.
    transport = new ReliableTransport(wirelessManager);
    pdnContextChannel = transport->channel<PdnConnectionContext>(
        PktType::kPdnConnectionContext);
    if (pdnContextChannel != nullptr) {
        pdnContextChannel->onReceive(
            [this](const uint8_t* fromMac, const PdnConnectionContext& ctx) {
                onContextReceived(fromMac, DeviceType::PDN, ctx.chainRole, ctx.player.userId,
                                  reinterpret_cast<const uint8_t*>(&ctx.player),
                                  sizeof(ctx.player));
            });
    }
    fdnContextChannel = transport->channel<FdnConnectionContext>(
        PktType::kFdnConnectionContext);
    if (fdnContextChannel != nullptr) {
        fdnContextChannel->onReceive(
            [this](const uint8_t* fromMac, const FdnConnectionContext& ctx) {
                // An FDN carries no player id, so its jack reports none.
                onContextReceived(fromMac, DeviceType::FDN, ctx.chainRole, PEER_USER_ID_NONE,
                                  reinterpret_cast<const uint8_t*>(&ctx.fdn),
                                  sizeof(ctx.fdn));
            });
    }

    // Head-roster channels (#158). Abandons are no-ops: an unreachable head means
    // the member simply stays unconfirmed (or the roster edit is lost) until the
    // chain state changes and re-triggers the send — the accepted residual of the
    // no-ack design.
    connectionAnnounceChannel = transport->channel<ConnectionAnnouncePayload>(
        PktType::kConnectionAnnounce);
    if (connectionAnnounceChannel != nullptr) {
        connectionAnnounceChannel->onReceive(
            [this](const uint8_t* fromMac, const ConnectionAnnouncePayload& announce) {
                onConnectionAnnounce(fromMac, announce);
            });
        // SEND_SUCCESS on the announce IS its delivery signal (#144): only then
        // may the HELLO confirmed bit rise. A head change mid-flight makes an
        // older announce's delivery stale — the re-announce to the current head
        // is what gates confirmed — so the seqId must match the latest announce
        // sent AND the destination must still be the head currently held.
        // confirmed therefore means "the head's radio took it", not "the head
        // admitted me": a roster at MAX_CHAIN_MEMBERS drops the announce after
        // the ack.
        connectionAnnounceChannel->setOnDelivered([this](uint8_t seqId, const uint8_t* toMac) {
            if (seqId != pendingAnnounceSeqId) return;
            const uint64_t head = chainHeadState.load();
            const uint64_t headMac48 = head & HEAD_MAC_MASK;
            if (headMac48 == 0 || headMac48 != MacToUInt64(toMac)) return;
            chainHeadState.store(head | CONFIRMED_BIT);
        });
    }
    // KEEP_DISTINCT: each report is a distinct event, not current state. A
    // demoted device routinely forwards two children's reports to the same head
    // in one tick; per-target supersede would erase the first one's retry slot.
    disconnectReportChannel = transport->channel<DisconnectReportPayload>(
        PktType::kDisconnectReport, {}, Resender::SendMode::KEEP_DISTINCT);
    if (disconnectReportChannel != nullptr) {
        disconnectReportChannel->onReceive(
            [this](const uint8_t* fromMac, const DisconnectReportPayload& report) {
                onDisconnectReport(fromMac, report);
            });
        // Only the newest report is tracked for the head-change re-send; an
        // older report's late delivery must not clear the newer pending one.
        disconnectReportChannel->setOnDelivered([this](uint8_t seqId, const uint8_t*) {
            if (seqId != pendingReportSeqId) return;
            pendingReportMac.fill(0);
        });
    }
    headTransferChannel = transport->channel<HeadTransferPayload>(
        PktType::kHeadTransfer);
    if (headTransferChannel != nullptr) {
        headTransferChannel->onReceive(
            [this](const uint8_t* fromMac, const HeadTransferPayload& transfer) {
                onHeadTransfer(fromMac, transfer);
            });
    }

    // Last: the jacks start carrying HELLO only once every channel a link event
    // can reach is claimed.
    enableHelloConnectivity();
}

void RemoteDeviceCoordinator::sync(Device* PDN) {
    // Platform-loop-owns-the-pump: drive reliable-transport retransmits/abandon
    // here every tick. Game managers must not pump it.
    if (transport != nullptr) transport->sync();

    for (SerialIdentifier port : HELLO_JACKS) {
        HelloLinkMachine* machine = helloByPort[portIndex(port)].machine;
        if (machine) machine->onStateLoop(PDN);
    }
    // Three separate sites move the derived role: a link commits to CONNECTED in
    // the loop above, dies through the Idle mount inside it, or latches a ring
    // during HELLO parsing between ticks. Poll once per tick to catch all three.
    // onRingClosed keeps firing at its own decision site (it is the shootout
    // coordinator claim point), so a ring closure and the RING role it implies
    // can be up to one tick apart.
    maybeFireChainRoleChange();
}

size_t RemoteDeviceCoordinator::portIndex(SerialIdentifier port) const {
    switch (port) {
        case SerialIdentifier::INPUT_JACK:           return 0;
        case SerialIdentifier::OUTPUT_JACK:          return 1;
        case SerialIdentifier::INPUT_JACK_SECONDARY: return 2;
        default:                                     return 1;
    }
}

PortStatus RemoteDeviceCoordinator::getPortStatus(SerialIdentifier port) {
    // A port is CONNECTED or it isn't. Chain position is a device-level fact
    // (getChainRole()).
    return mapHelloLinkToStatus(port);
}

PortState RemoteDeviceCoordinator::getPortState(SerialIdentifier port) {
    std::vector<std::array<uint8_t, 6>> peerAddresses;

    // Composed from getPeerMac so a jack cannot report one direct peer here and
    // a different one there.
    const uint8_t* directPeer = getPeerMac(port);
    if (directPeer != nullptr) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), directPeer, 6);
        peerAddresses.push_back(mac);
    }

    return PortState{ port, getPortStatus(port), peerAddresses };
}

void RemoteDeviceCoordinator::setChainChangeCallback(std::function<void()> callback) {
    chainChangeCallback = std::move(callback);
}

void RemoteDeviceCoordinator::setPeerLostCallback(std::function<void(const uint8_t*)> callback) {
    peerLostCallback = std::move(callback);
}

DeviceType RemoteDeviceCoordinator::getPeerDeviceType(SerialIdentifier port) const {
    // The kind comes from the peer's context, so it reads UNKNOWN until that
    // exchange completes.
    return helloByPort[portIndex(port)].peerDeviceType;
}

const uint8_t* RemoteDeviceCoordinator::getPeerMac(SerialIdentifier port) const {
    const HelloLinkMachine* machine = helloByPort[portIndex(port)].machine;
    // An Idle jack has no peer. It can already hold the MAC that arms next
    // tick's commit to Connecting, and serving that would contradict the same
    // jack reporting DISCONNECTED.
    if (machine == nullptr || machine->currentStateId() == HELLO_LINK_IDLE) {
        return nullptr;
    }
    return machine->peer().data();
}

bool RemoteDeviceCoordinator::isDirectPeer(const uint8_t* mac) const {
    if (!mac) return false;
    for (SerialIdentifier port : HELLO_JACKS) {
        const uint8_t* peer = getPeerMac(port);
        if (peer && memcmp(peer, mac, 6) == 0) return true;
    }
    return false;
}

bool RemoteDeviceCoordinator::canReachPeer(const uint8_t* mac) const {
    return isDirectPeer(mac);
}

void RemoteDeviceCoordinator::registerPeer(const uint8_t* macAddress) {
    if (wirelessManager_ != nullptr) {
        wirelessManager_->addEspNowPeer(macAddress);
    }
}

void RemoteDeviceCoordinator::notifyChainChange() {
    // Copied before the call for the same reason the jack-change dispatch does
    // it: a handler that reacts by tearing its own state down clears this slot,
    // destroying the std::function whose operator() frame is still live.
    std::function<void()> handler = chainChangeCallback;
    if (handler) handler();
}

void RemoteDeviceCoordinator::unregisterPeer(const uint8_t* macAddress) {
    if (wirelessManager_ != nullptr) {
        wirelessManager_->removeEspNowPeer(macAddress);
    }
}

// ---- Per-jack HELLO connectivity (#155) ----

unsigned long RemoteDeviceCoordinator::nowMs() {
    PlatformClock* clock = SimpleTimer::getPlatformClock();
    return clock != nullptr ? clock->milliseconds() : 0;
}

HWSerialWrapper* RemoteDeviceCoordinator::jackWrapper(SerialIdentifier port) const {
    if (serialManager == nullptr) return nullptr;
    switch (port) {
        case SerialIdentifier::OUTPUT_JACK: return serialManager->getOutputJack();
        case SerialIdentifier::INPUT_JACK: return serialManager->getInputJack();
        case SerialIdentifier::INPUT_JACK_SECONDARY: return serialManager->getSecondaryInputJack();
        default: return nullptr;
    }
}

void RemoteDeviceCoordinator::enableHelloConnectivity() {
    if (wirelessManager_ != nullptr) {
        const uint8_t* mac = wirelessManager_->getMacAddress();
        if (mac != nullptr) memcpy(selfMac.data(), mac, 6);
    }

    for (SerialIdentifier port : HELLO_JACKS) {
        HWSerialWrapper* hw = jackWrapper(port);
        if (hw == nullptr) continue;
        JackHelloLink& link = helloByPort[portIndex(port)];
        link.parser.setHelloFrameHandler(
            [this, port](const HelloPayload& hello) { onHelloReceived(port, hello); });
        // exec() runs on the main loop, so the parser is fed single-threaded.
        hw->setByteCallback([this, port](uint8_t b) {
            helloByPort[portIndex(port)].parser.feed(&b, 1);
        });

        HelloLinkContext context;
        context.jack = port;
        context.nowMs = [] { return RemoteDeviceCoordinator::nowMs(); };
        context.onContextRequest = [this](SerialIdentifier j) {
            initiateContextExchange(j);
        };
        context.onJackChange = [this](SerialIdentifier j, bool connected) {
            // The upstream link reaching Connected is the moment its advertised
            // head becomes adoptable, and the game layer must see the resulting
            // chain state on the connect it is told about — so adopt first (#158).
            if (connected && j == SerialIdentifier::INPUT_JACK) adoptUpstreamHead();
            // Copied before the call: a handler that reacts by dismounting its own
            // state clears this slot, which would destroy the std::function whose
            // operator() frame is still live.
            JackChangeCallback handler = jackChangeCallback;
            if (handler) handler(j, connected);
            // Connects only. A disconnect reaches here during the Connected
            // state's dismount, while this jack still names its departing peer
            // and still reads CONNECTED, so the chain state a subscriber would
            // read is the one that just stopped being true. onLinkDown below
            // reports that edge instead, from Idle, where the accessors agree.
            if (connected) notifyChainChange();
        };
        // Every link-death path mounts Idle; the initial mount fires this too, a
        // no-op on zero state.
        context.onLinkDown = [this, port](SerialIdentifier j, const std::array<uint8_t, 6>& mac) {
            helloByPort[portIndex(port)].parser.requestReset();
            if (MacToUInt64(mac.data()) != 0) {
                releaseHelloPeer(j, mac.data());
                // Downstream (OUTPUT) departures are this device's to report: it
                // is the only node that sees that HELLO go silent (#158).
                if (j == SerialIdentifier::OUTPUT_JACK) reportDownstreamLoss(mac.data());
                std::function<void(const uint8_t*)> lost = peerLostCallback;
                if (lost) lost(mac.data());
            }
            onLinkLost(port);
            // This is where a link death is reported: Idle is mounted, so the jack
            // now answers IDLE and hands back no peer. Reporting it from the jack
            // change instead would describe the link as still up — that callback
            // runs during the Connected state's dismount — and a subscriber has no
            // way to ask again later, so a peer returning on the same MAC would
            // look unchanged to everyone downstream.
            notifyChainChange();
        };
        context.silentLinkMs = HELLO_SILENT_LINK_MS;
        context.contextTimeoutMs = CONTEXT_EXCHANGE_TIMEOUT_MS;
        link.machine = new HelloLinkMachine(std::move(context));
        link.machine->initialize(nullptr);  // states ignore Device*; mounts Idle
    }

#ifndef NATIVE_BUILD
    if (!externalConnectivityTask && connectivityTaskHandle == nullptr) {
        xTaskCreate(connectivityTaskEntry, "rdc-hello", kConnectivityTaskStack, this,
                    kConnectivityTaskPriority, &connectivityTaskHandle);
    }
#endif
}

void RemoteDeviceCoordinator::emitHello() {
    HelloPayload hello{};
    memcpy(hello.source, selfMac.data(), 6);
    hello.deviceType = static_cast<uint8_t>(selfDeviceType);
    // Single atomic load: the main loop is the sole writer, so one snapshot of the
    // packed head/confirmed word is internally consistent. All-zero headMac means
    // "I am the head/standalone"; confirmed rises only once the announce to that
    // head is delivered.
    const uint64_t head = chainHeadState.load();
    const uint64_t mac48 = head & HEAD_MAC_MASK;
    if (mac48 != 0) unpackMac(mac48, hello.headMac);
    if ((head & CONFIRMED_BIT) != 0) hello.flags |= HELLO_FLAG_CONFIRMED;
    if (isInRing()) hello.flags |= HELLO_FLAG_RING_CLOSED;

    const std::vector<uint8_t> frame = encodeFramed(hello);
    for (SerialIdentifier port : HELLO_JACKS) {
        HWSerialWrapper* hw = jackWrapper(port);
        if (hw == nullptr) continue;
        for (uint8_t b : frame)
            hw->print(static_cast<char>(b));
    }
}

void RemoteDeviceCoordinator::onHelloReceived(SerialIdentifier jack, const HelloPayload& hello) {
    // Reject an all-zero source (open jack) and our own echo before any liveness
    // stamp: the output TX pin can loop back through the TRS contacts, and a
    // self-HELLO must never keep a dead cable "alive" or fake a peer. The RDC owns
    // selfMac, so this guard stays here rather than in the link SM.
    if (MacToUInt64(hello.source) == 0) return;
    if (memcmp(hello.source, selfMac.data(), 6) == 0) return;

    // The machine records the peer MAC and, for a first HELLO on any Idle jack,
    // fires onContextRequest -> initiateContextExchange as it enters Connecting.
    JackHelloLink& link = helloByPort[portIndex(jack)];
    if (link.machine) link.machine->onHelloReceived(hello);

    // Head inheritance + ring detection are directional: only the upstream (INPUT)
    // jack drives them, so the head's MAC cascades downstream. The FDN secondary
    // input jack is a symbol link and takes no part in the chain.
    if (jack == SerialIdentifier::INPUT_JACK) {
        applyUpstreamHead(hello);
    }
}

void RemoteDeviceCoordinator::initiateContextExchange(SerialIdentifier jack) {
    HelloLinkMachine* machine = helloByPort[portIndex(jack)].machine;
    if (machine == nullptr) return;
    const uint8_t* mac = machine->peer().data();
    // Radio slot first (the driver add is idempotent): the drain below can announce
    // the peer to game code, and a pending send from our other jack skips the send
    // path entirely — neither may run before the peer has its ESP-NOW slot.
    registerPeer(mac);
    // Apply any context that beat this jack's HELLO and was cached while it was still
    // Idle. Runs for every jack entering Connecting, before the send-collapse below,
    // so the second jack of a 2-node ring still gets the cached context.
    drainBufferedContext(jack, mac);
    // In a 2-node ring both our jacks face the same peer and connect in the same
    // tick; our context is identical on each, so one copy suffices. Skip if a send
    // to this MAC is already pending rather than putting a second frame on the air
    // (the Resender transmits each send immediately, so it would NOT be collapsed).
    if (isContextSendPending(mac)) return;
    sendSelfContext(mac);
}

void RemoteDeviceCoordinator::sendSelfContext(const uint8_t* mac) {
    // Each device describes itself, so the payload is chosen by THIS device's kind,
    // not the peer's, and the role is read at send time: a resend on a role change
    // has to carry the position this device holds now, not the one it held at plug-in.
    const uint8_t role = static_cast<uint8_t>(getChainRole());
    if (selfDeviceType == DeviceType::FDN) {
        if (fdnContextChannel == nullptr) return;
        FdnConnectionContext ctx{};
        ctx.chainRole = role;
        fdnContextChannel->sendReliable(mac, ctx);
        return;
    }
    if (pdnContextChannel == nullptr) return;
    PdnConnectionContext ctx{};
    ctx.chainRole = role;
    if (selfProfileProvider) ctx.player = selfProfileProvider();
    pdnContextChannel->sendReliable(mac, ctx);
}

void RemoteDeviceCoordinator::resendContext() {
    // No pending-send guard, unlike the initiate path: there the queued payload is
    // the one we would re-queue, here it is the STALE one this call exists to
    // replace. The context channels supersede per target, so the fresh send drops
    // the unacked entry and its old bytes.
    std::array<std::array<uint8_t, 6>, kNumPorts> alreadySent{};
    size_t sentCount = 0;
    for (SerialIdentifier port : HELLO_JACKS) {
        const HelloLinkMachine* machine = helloByPort[portIndex(port)].machine;
        if (machine == nullptr || machine->currentStateId() != HELLO_LINK_CONNECTED) continue;
        const uint8_t* mac = machine->peer().data();
        // A 2-node ring faces the same peer on more than one jack and one copy
        // covers them all. Every MAC covered so far is checked, not just the last
        // one: the jacks sharing a peer need not be adjacent in HELLO_JACKS.
        bool covered = false;
        for (size_t i = 0; i < sentCount; ++i) {
            if (memcmp(alreadySent[i].data(), mac, 6) == 0) covered = true;
        }
        if (covered) continue;
        memcpy(alreadySent[sentCount++].data(), mac, 6);
        sendSelfContext(mac);
    }
}

bool RemoteDeviceCoordinator::isContextSendPending(const uint8_t* mac) const {
    if (pdnContextChannel != nullptr && pdnContextChannel->isPending(mac)) return true;
    if (fdnContextChannel != nullptr && fdnContextChannel->isPending(mac)) return true;
    return false;
}

void RemoteDeviceCoordinator::onContextReceived(const uint8_t* fromMac, DeviceType peerType,
                                                uint8_t chainRole, uint16_t peerUserId,
                                                const uint8_t* profile, size_t len) {
    // Every jack sends its own context on connecting, so receiving the peer's just
    // completes our matching jack(s); no reply in the normal exchange. Cache it keyed by MAC AND
    // apply it now: caching (not only applying) covers a jack that reaches CONNECTING
    // AFTER this arrives. That happens two ways — a context beats its own HELLO
    // (ESP-NOW vs serial) so no jack is CONNECTING yet, and in a 2-node ring the two
    // jacks facing this peer enter CONNECTING one tick apart (sync() drives them in
    // order), so the later jack must still find the context. The TTL clears the cache.
    bufferContext(fromMac, peerType, chainRole, peerUserId, profile, len);
    applyContextToJacks(fromMac, peerType, chainRole, peerUserId, profile, len);
}

void RemoteDeviceCoordinator::applyContextToJacks(const uint8_t* fromMac, DeviceType peerType,
                                                  uint8_t chainRole, uint16_t peerUserId,
                                                  const uint8_t* profile, size_t len) {
    // Live receive path: a 2-node ring points both our jacks at the same peer with
    // both already CONNECTING, so one context completes both. The peer is already a
    // radio slot (registered when our jack initiated), so nothing to register here.
    bool appliedToConnectingJack = false;
    for (SerialIdentifier port : HELLO_JACKS) {
        HelloLinkMachine* machine = helloByPort[portIndex(port)].machine;
        if (machine == nullptr || machine->currentStateId() != HELLO_LINK_CONNECTING) continue;
        if (memcmp(machine->peer().data(), fromMac, 6) != 0) continue;
        completeJackContext(port, peerType, chainRole, peerUserId, profile, len);
        appliedToConnectingJack = true;
    }
    if (appliedToConnectingJack) return;

    // A context from a MAC a CONNECTED jack already tracks means the peer is still
    // cycling CONNECTING — our earlier context was lost app-side (SEND_SUCCESS is
    // MAC-ack only) or the peer rebooted and its first resend was deduped. Resend
    // ours so its cycle completes. No ping-pong: only the CONNECTING side retries,
    // and it stops once this lands.
    for (SerialIdentifier port : HELLO_JACKS) {
        JackHelloLink& link = helloByPort[portIndex(port)];
        if (link.machine == nullptr || link.machine->currentStateId() != HELLO_LINK_CONNECTED)
            continue;
        if (memcmp(link.machine->peer().data(), fromMac, 6) != 0) continue;
        // Throttle to one resend per exchange window: every send stamps a fresh
        // seqId, so dedup can't brake two CONNECTED sides answering each other —
        // unthrottled they'd volley at radio RTT until link death.
        const unsigned long now = nowMs();
        if (link.lastContextResendMs != 0 && now >= link.lastContextResendMs &&
            now - link.lastContextResendMs < CONTEXT_EXCHANGE_TIMEOUT_MS)
            return;
        if (!isContextSendPending(fromMac)) {
            sendSelfContext(fromMac);
            link.lastContextResendMs = now;
        }
        return;
    }
}

void RemoteDeviceCoordinator::completeJackContext(SerialIdentifier jack, DeviceType peerType,
                                                  uint8_t chainRole, uint16_t peerUserId,
                                                  const uint8_t* profile, size_t len) {
    // A second fresh-seqId context can land before the Connecting -> Connected
    // commit while the jack still reports CONNECTING; the game callback must
    // fire exactly once per connect.
    JackHelloLink& link = helloByPort[portIndex(jack)];
    if (link.machine != nullptr && link.machine->didMarkContextComplete()) return;
    // The kind comes from the channel that decoded this context, not the HELLO
    // deviceType byte: the two are independently spoofable, and a consumer reading
    // peerDeviceType to decide how to cast the profile bytes needs the kind that
    // produced them.
    link.peerDeviceType = peerType;
    link.peerChainRole = chainRole;
    // The bound is belt-and-braces: ReliableChannel rejects any frame whose length
    // is not exactly sizeof(P), so both callers pass a compile-time size that fits.
    link.peerProfileLen = std::min(len, link.peerProfile.size());
    memcpy(link.peerProfile.data(), profile, link.peerProfileLen);
    link.peerUserId = peerUserId;
    if (contextReceivedCallback) contextReceivedCallback(jack, peerType, profile, len);
    onContextExchangeComplete(jack);
}

void RemoteDeviceCoordinator::bufferContext(const uint8_t* fromMac, DeviceType peerType,
                                            uint8_t chainRole, uint16_t peerUserId,
                                            const uint8_t* profile, size_t len) {
    const unsigned long now = nowMs();
    BufferedContext* slot = nullptr;
    BufferedContext* oldest = &contextBuffer[0];
    for (BufferedContext& e : contextBuffer) {
        if (e.valid && memcmp(e.mac.data(), fromMac, 6) == 0) {  // latest for a MAC wins
            slot = &e;
            break;
        }
        const bool reusable = !e.valid || e.isExpiredAt(now);
        if (reusable && slot == nullptr) slot = &e;
        if (e.arrivedAtMs < oldest->arrivedAtMs) oldest = &e;
    }
    if (slot == nullptr) slot = oldest;  // full of fresh entries: evict the oldest

    memcpy(slot->mac.data(), fromMac, 6);
    slot->peerType = peerType;
    slot->chainRole = chainRole;
    slot->peerUserId = peerUserId;
    slot->len = len < slot->profile.size() ? len : slot->profile.size();
    memcpy(slot->profile.data(), profile, slot->len);
    slot->arrivedAtMs = now;
    slot->valid = true;
}

void RemoteDeviceCoordinator::drainBufferedContext(SerialIdentifier jack, const uint8_t* mac) {
    const unsigned long now = nowMs();
    for (BufferedContext& e : contextBuffer) {
        if (!e.valid) continue;
        if (e.isExpiredAt(now)) {
            e.valid = false;  // expired before its jack ever connected (stale/attacker)
            continue;
        }
        if (memcmp(e.mac.data(), mac, 6) != 0) continue;
        // Apply to THIS jack only, and leave the entry valid: the peer's other jack (a
        // 2-node ring) drains its own copy when it connects a tick later. Applying
        // per-jack keeps the context callback firing exactly once per jack. TTL clears it.
        completeJackContext(jack, e.peerType, e.chainRole, e.peerUserId, e.profile.data(), e.len);
    }
}

void RemoteDeviceCoordinator::releaseHelloPeer(SerialIdentifier jack, const uint8_t* mac) {
    // Per-jack residue clears unconditionally: the keep-slot guards below are
    // per-MAC, and the next peer on this jack must not inherit the departed
    // peer's chainRole during its CONNECTING window (#156 reads it then).
    helloByPort[portIndex(jack)].peerChainRole = 0;
    helloByPort[portIndex(jack)].peerDeviceType = DeviceType::UNKNOWN;
    helloByPort[portIndex(jack)].peerUserId = PEER_USER_ID_NONE;
    helloByPort[portIndex(jack)].peerProfileLen = 0;
    // Defence in depth: the zeroed length already hides the bytes from
    // getPeerProfile, but a departed peer's identity should not sit in RAM.
    helloByPort[portIndex(jack)].peerProfile.fill(0);
    helloByPort[portIndex(jack)].lastContextResendMs = 0;
    // A 2-node ring has the same peer on both jacks: releasing the radio slot on a
    // one-cable disconnect would silently break wireless for the still-connected
    // link, so any other jack tracking this MAC keeps the slot alive.
    for (SerialIdentifier port : HELLO_JACKS) {
        if (port == jack) continue;
        const uint8_t* peer = getPeerMac(port);
        if (peer != nullptr && memcmp(peer, mac, 6) == 0) return;
    }
    // A peer past the other-jack scan has left this jack entirely, so its
    // context retries are dead regardless of whether the slot survives below: a
    // retransmit re-registers its target inside the driver (EnsurePeerIsRegistered),
    // which would re-add a released slot and leak it. cancel() is silent — no
    // abandon callback fires.
    if (pdnContextChannel != nullptr) pdnContextChannel->cancel(mac);
    if (fdnContextChannel != nullptr) fdnContextChannel->cancel(mac);
    // The held head keeps its slot: it stays the roster unicast target even
    // when it is no longer adjacent (adjacency is what just ended here).
    if ((chainHeadState.load() & HEAD_MAC_MASK) == MacToUInt64(mac)) return;
    unregisterPeer(mac);
}

void RemoteDeviceCoordinator::onContextExchangeComplete(SerialIdentifier jack) {
    HelloLinkMachine* machine = helloByPort[portIndex(jack)].machine;
    if (machine) machine->onContextExchangeComplete();
}

RemoteDeviceCoordinator::HelloLinkState
RemoteDeviceCoordinator::getHelloLinkState(SerialIdentifier jack) const {
    const HelloLinkMachine* machine = helloByPort[portIndex(jack)].machine;
    if (machine == nullptr) return HelloLinkState::IDLE;
    switch (machine->currentStateId()) {
        case HELLO_LINK_CONNECTED:  return HelloLinkState::CONNECTED;
        case HELLO_LINK_CONNECTING: return HelloLinkState::CONNECTING;
        default:                    return HelloLinkState::IDLE;
    }
}

PortStatus RemoteDeviceCoordinator::mapHelloLinkToStatus(SerialIdentifier port) const {
    const HelloLinkMachine* machine = helloByPort[portIndex(port)].machine;
    if (machine == nullptr) return PortStatus::DISCONNECTED;
    switch (machine->currentStateId()) {
        case HELLO_LINK_CONNECTED:  return PortStatus::CONNECTED;
        case HELLO_LINK_CONNECTING: return PortStatus::CONNECTING;
        default:                    return PortStatus::DISCONNECTED;
    }
}

uint64_t RemoteDeviceCoordinator::packHead(const uint8_t* mac, bool confirmed) {
    uint64_t value = MacToUInt64(mac) & HEAD_MAC_MASK;
    if (confirmed) value |= CONFIRMED_BIT;
    return value;
}

void RemoteDeviceCoordinator::unpackMac(uint64_t value, uint8_t* out) {
    for (int i = 5; i >= 0; --i) {
        out[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
    }
}

bool RemoteDeviceCoordinator::isInRing() const {
    if (ringLatched) return true;
    // Relayed membership. An upstream asserting closure only concerns this
    // device while the loop can still run through it, so both chain links must
    // be up — holding a head already implies a Connected INPUT, and the OUTPUT
    // check makes a pulled downstream cable drop membership here immediately
    // rather than waiting for the head's own evidence window.
    // Requiring a held head is also what keeps the relay acyclic: the latching
    // device holds none, so its assertion is never fed back to it and clearing
    // its latch drains the flag round the loop.
    if ((chainHeadState.load() & HEAD_MAC_MASK) == 0) return false;
    if (getHelloLinkState(SerialIdentifier::OUTPUT_JACK) != HelloLinkState::CONNECTED) return false;
    return upstreamRingClosed;
}

ChainRole RemoteDeviceCoordinator::getChainRole() const {
    if (ringLatched) return ChainRole::RING;
    if (getHelloLinkState(SerialIdentifier::INPUT_JACK) == HelloLinkState::CONNECTED) {
        return ChainRole::CHILD;
    }
    if (getHelloLinkState(SerialIdentifier::OUTPUT_JACK) == HelloLinkState::CONNECTED) {
        return ChainRole::HEAD;
    }
    return ChainRole::STANDALONE;
}

const uint8_t* RemoteDeviceCoordinator::getHeadMac() const {
    const uint64_t mac48 = chainHeadState.load() & HEAD_MAC_MASK;
    if (mac48 == 0) return nullptr;
    unpackMac(mac48, headMacScratch.data());
    return headMacScratch.data();
}

void RemoteDeviceCoordinator::applyUpstreamHead(const HelloPayload& hello) {
    upstreamRingClosed = (hello.flags & HELLO_FLAG_RING_CLOSED) != 0;

    // The upstream peer's effective head: its advertised headMac if it has one,
    // else its own MAC (meaning the peer is itself the head). Inheritance is not
    // an election — the head's MAC flows down the chain unchallenged; MACs are
    // compared only to break the latched-head conflict below.
    const uint8_t* peerEffectiveHead =
        MacToUInt64(hello.headMac) != 0 ? hello.headMac : hello.source;

    const bool peerHeadIsSelf =
        memcmp(peerEffectiveHead, selfMac.data(), 6) == 0;

    // A latched ring hears its own seeded head return on INPUT every cycle; that
    // return IS the evidence the loop still closes through us, so stamp it.
    if (ringLatched && peerHeadIsSelf) lastSelfHeadReturnMs = nowMs();

    // A different head on INPUT while latched is a head conflict: a second
    // latched head (symmetric 2-ring boot, two heads merging) or a replacement
    // head propagating hop-by-hop after a break elsewhere in the loop. Lower MAC
    // wins (#144): stand down at once for a lower head — unlatching
    // unconditionally lets two latched heads depose each other and re-latch in
    // lockstep, re-firing ringClosed every cycle. For a higher head, hold the
    // latch only while self-returns are fresh: in a merge transient our own
    // claim keeps circulating and refreshing the stamp, but after a physical
    // break it can no longer complete the loop, so the evidence times out and
    // the new head must be adopted.
    if (ringLatched && !peerHeadIsSelf) {
        if (MacToUInt64(peerEffectiveHead) > MacToUInt64(selfMac.data()) &&
            nowMs() - lastSelfHeadReturnMs <= RING_EVIDENCE_TIMEOUT_MS) {
            return;
        }
        ringLatched = false;
    }

    // Ring: this device's own MAC returns on its INPUT jack, so the head it seeded
    // traveled the whole loop back. Gate on OUTPUT being connected (a downstream
    // exists), NOT on role==HEAD — two already-connected sub-chains merging into a
    // ring deliver our head back on an INPUT that is already CONNECTED, where
    // role==HEAD (which demands INPUT be un-connected) would miss the closure.
    if (!ringLatched && peerHeadIsSelf &&
        getHelloLinkState(SerialIdentifier::OUTPUT_JACK) == HelloLinkState::CONNECTED) {
        ringLatched = true;
        // Seed the evidence stamp at closure so a break immediately after still
        // has a valid baseline to time out from.
        lastSelfHeadReturnMs = nowMs();
        // Our own MAC came back, so we ARE the head: drop any head adopted while the
        // ring was forming, or we would sit in RING advertising a stale foreign head.
        const uint64_t formingHead = chainHeadState.load() & HEAD_MAC_MASK;
        chainHeadState.store(0);
        // A downstream-loss report is owed only while we remain a child under a
        // head. Becoming our own head voids it: clear the pending re-send state so
        // a later head adoption (applyUpstreamHead's successor-chase) cannot ship
        // this stale report to an unrelated new head and prune a live member.
        pendingReportMac.fill(0);
        pendingReportSeqId = 0;
        releaseHeadPeer(formingHead);
        // Copied before the call, as the chain-change and peer-lost dispatches are:
        // the subscriber is a game-layer manager that clears this slot in its own
        // destructor, so a handler reaching a teardown would free the std::function
        // whose operator() frame is still live.
        RingClosedCallback ringClosed = ringClosedCallback;
        if (ringClosed) ringClosed();
        return;
    }

    // A device never inherits its own MAC as a head (that is what "I am head"
    // encodes as an absent head_mac); the ring case above already handled it.
    if (peerHeadIsSelf) return;

    // Record what the upstream claims and let the link machine decide whether it
    // counts: a HELLO is one-way evidence, adoption is not.
    memcpy(upstreamAdvertisedHead.data(), peerEffectiveHead, 6);
    adoptUpstreamHead();
}

void RemoteDeviceCoordinator::adoptUpstreamHead() {
    // The INPUT link machine is the single authority on whether an upstream is
    // real. A one-way cable delivers its HELLOs forever while this side never
    // establishes; adopting off those would advertise a head no downstream can
    // reach and hand this device's roster to a peer that cannot answer.
    if (getHelloLinkState(SerialIdentifier::INPUT_JACK) != HelloLinkState::CONNECTED) return;
    const uint64_t newHead = MacToUInt64(upstreamAdvertisedHead.data());
    if (newHead == 0) return;
    const uint64_t previousHead = chainHeadState.load() & HEAD_MAC_MASK;
    if (previousHead == newHead) return;
    const uint8_t* headMac = upstreamAdvertisedHead.data();

    // The head is a unicast target (announce/report/transfer) that is usually
    // not an adjacent HELLO peer, so its radio slot is managed here: claim the
    // successor's before any send below, drop the predecessor's after.
    registerPeer(headMac);
    // Adopt the upstream head, dropping to confirmed=0 until re-confirmed under
    // it. The new head then propagates in our own HELLO.
    chainHeadState.store(packHead(headMac, false));
    // A demoted head hands its roster to the successor (#158); for a plain child
    // the roster is empty and this no-ops. Then announce under the new head so
    // confirmed can rise again.
    transferRosterTo(headMac);
    maybeAnnounceToHead();
    // releaseHeadPeer below cancels the report channel to the old head, and
    // nothing else re-triggers a report (the announce path re-announces, the
    // report path has no equivalent), so an undelivered report must chase the
    // successor head or its member stays a phantom in that roster.
    if (MacToUInt64(pendingReportMac.data()) != 0) {
        sendDisconnectReport(headMac, pendingReportMac.data());
    }
    releaseHeadPeer(previousHead);
}

void RemoteDeviceCoordinator::onLinkLost(SerialIdentifier port) {
    // The FDN secondary input jack is a symbol link, not part of the INPUT/OUTPUT
    // chain, so its teardown must not open the ring or clear the inherited head.
    if (port == SerialIdentifier::INPUT_JACK_SECONDARY) return;

    // A ring closes through both this device's chain links; losing either the
    // upstream (INPUT) or downstream (OUTPUT) one breaks the loop, so open the ring.
    ringLatched = false;

    // Only the upstream (INPUT) peer supplies the inherited head; its loss makes
    // this device its own head again.
    if (port == SerialIdentifier::INPUT_JACK) {
        const uint64_t lostHead = chainHeadState.load() & HEAD_MAC_MASK;
        chainHeadState.store(0);
        // The departed peer's claim dies with it, or the next upstream to reach
        // Connected would be adopted as whatever this one last advertised.
        upstreamAdvertisedHead.fill(0);
        upstreamRingClosed = false;
        // A downstream-loss report is owed only while we remain a child under a
        // head. Becoming our own head voids it: clear the pending re-send state so
        // a later head adoption (applyUpstreamHead's successor-chase) cannot ship
        // this stale report to an unrelated new head and prune a live member.
        pendingReportMac.fill(0);
        pendingReportSeqId = 0;
        releaseHeadPeer(lostHead);
    }
}

void RemoteDeviceCoordinator::releaseHeadPeer(uint64_t headMac48) {
    if (headMac48 == 0) return;
    uint8_t mac[6];
    unpackMac(headMac48, mac);
    // Same keep-slot guard as releaseHelloPeer: an adjacent link still using
    // this MAC keeps the radio slot alive.
    for (SerialIdentifier port : HELLO_JACKS) {
        const uint8_t* peer = getPeerMac(port);
        if (peer != nullptr && memcmp(peer, mac, 6) == 0) return;
    }
    // Past the keep-slot guard: the head truly departed (no adjacent link still
    // names it). Only on this actual-release path are its roster retries dead
    // traffic; drop them with the slot so a retransmit can't re-register it inside
    // the driver. When a keep-slot guard retained the slot above, the retries are
    // left to self-heal (a seqId bump ignores late acks).
    if (connectionAnnounceChannel != nullptr) connectionAnnounceChannel->cancel(mac);
    if (disconnectReportChannel != nullptr) disconnectReportChannel->cancel(mac);
    if (headTransferChannel != nullptr) headTransferChannel->cancel(mac);
    unregisterPeer(mac);
}

void RemoteDeviceCoordinator::maybeFireChainRoleChange() {
    const ChainRole role = getChainRole();
    if (role == lastChainRole) return;
    lastChainRole = role;
    // Copied before the call; see the ring-closed dispatch for why.
    ChainRoleChangeCallback roleChange = chainRoleChangeCallback;
    if (roleChange) roleChange(role);
}

// ---- Head roster (#158) ----

bool RemoteDeviceCoordinator::isRosterAuthority() const {
    // Only the device currently heading a chain (or a latched ring, its own
    // head) may edit a roster. Gating on role rather than "no head held"
    // matters for STANDALONE: an ex-head keeps an all-zero head word after its
    // chain dies, and late Resender retries from members would otherwise seed
    // phantom entries that resurface the next time it heads a chain.
    const ChainRole role = getChainRole();
    return role == ChainRole::HEAD || role == ChainRole::RING;
}

std::vector<std::array<uint8_t, 6>> RemoteDeviceCoordinator::getChainMembers() const {
    // A RING device is its own head, so it serves its roster like a HEAD does.
    if (!isRosterAuthority()) return {};
    std::vector<std::array<uint8_t, 6>> members;
    members.reserve(chainRoster.size());
    for (const auto& entry : chainRoster)
        members.push_back(entry.first);
    return members;
}

void RemoteDeviceCoordinator::maybeAnnounceToHead() {
    if (connectionAnnounceChannel == nullptr) return;
    const uint64_t headMac48 = chainHeadState.load() & HEAD_MAC_MASK;
    // No upstream head held: this device IS its chain's head (or standalone, or a
    // latched ring, which stores no head). Nothing to announce, no self-sends.
    if (headMac48 == 0) return;
    // The announce names the upstream neighbour, which is known because a head is
    // only ever held while that link is Connected (adoptUpstreamHead).
    const HelloLinkMachine* upstream =
        helloByPort[portIndex(SerialIdentifier::INPUT_JACK)].machine;
    if (upstream == nullptr) return;
    uint8_t headMac[6];
    unpackMac(headMac48, headMac);
    ConnectionAnnouncePayload announce{};
    memcpy(announce.upstreamMac, upstream->peer().data(), 6);
    pendingAnnounceSeqId = connectionAnnounceChannel->sendReliable(headMac, announce);
}

void RemoteDeviceCoordinator::reportDownstreamLoss(const uint8_t* lostMac) {
    const uint64_t headMac48 = chainHeadState.load() & HEAD_MAC_MASK;
    if (headMac48 == 0) {
        // This device is the head (incl. a latched ring) and its only OUTPUT
        // link died, so the entire chain below is unreachable — not just
        // lostMac's subtree. A full clear also flushes any entry whose recorded
        // upstream path had gone gappy and a subtree walk would miss.
        if (!chainRoster.empty()) {
            chainRoster.clear();
            if (membershipChangeCallback) membershipChangeCallback();
        }
        return;
    }
    // Losing the cable to our own head (a ring's closing link) strands nobody:
    // every member is still chained beneath the head. Suppressing the
    // head-naming report here is the outer of two guards — it avoids dead
    // traffic; the head's own self-report reject is the trust boundary.
    if (headMac48 == MacToUInt64(lostMac)) return;
    uint8_t headMac[6];
    unpackMac(headMac48, headMac);
    sendDisconnectReport(headMac, lostMac);
}

void RemoteDeviceCoordinator::sendDisconnectReport(const uint8_t* headMac,
                                                   const uint8_t* lostMac) {
    if (disconnectReportChannel == nullptr) return;
    DisconnectReportPayload report{};
    memcpy(report.disconnectedMac, lostMac, 6);
    pendingReportSeqId = disconnectReportChannel->sendReliable(headMac, report);
    // memmove, not memcpy: the successor-chase re-send passes pendingReportMac
    // back in as lostMac, so source and destination alias on that path.
    memmove(pendingReportMac.data(), lostMac, 6);
}

void RemoteDeviceCoordinator::removeChainMemberSubtree(const uint8_t* mac) {
    // Everything downstream of the departed MAC is unreachable too. Forks are
    // impossible by construction (transfers skip a member whose upstream slot is
    // already held via isUpstreamHeldByOther, announces evict stale claimants),
    // so this walk is a list traversal that finds at
    // most one child per step by scanning — no reverse index kept. Tolerating
    // multiple children anyway is cheap defense. `gone` doubles as the frontier.
    std::vector<std::array<uint8_t, 6>> gone;
    gone.emplace_back();
    memcpy(gone.back().data(), mac, 6);
    bool changed = chainRoster.erase(gone.back()) > 0;
    for (size_t i = 0; i < gone.size(); ++i) {
        for (auto it = chainRoster.begin(); it != chainRoster.end();) {
            if (it->second == gone[i]) {
                gone.push_back(it->first);
                it = chainRoster.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    if (changed && membershipChangeCallback) membershipChangeCallback();
}

void RemoteDeviceCoordinator::transferRosterTo(const uint8_t* newHeadMac) {
    if (chainRoster.empty() || headTransferChannel == nullptr) return;
    HeadTransferPayload transfer{};
    for (const auto& entry : chainRoster) {
        // Both insert paths cap at MAX_CHAIN_MEMBERS; this bounds the payload
        // array regardless.
        if (transfer.memberCount >= MAX_CHAIN_MEMBERS) break;
        memcpy(transfer.memberMacs[transfer.memberCount], entry.first.data(), 6);
        memcpy(transfer.upstreamMacs[transfer.memberCount], entry.second.data(), 6);
        transfer.memberCount++;
    }
    headTransferChannel->sendReliable(newHeadMac, transfer);
    chainRoster.clear();
    if (membershipChangeCallback) membershipChangeCallback();
}

void RemoteDeviceCoordinator::onConnectionAnnounce(const uint8_t* fromMac,
                                                   const ConnectionAnnouncePayload& announce) {
    // Only the roster authority accepts announces; a stale one reaching a
    // demoted head is dropped, and the member re-announces once the new head
    // propagates via HELLO. An announce racing this head's own OUTPUT
    // Connected commit is dropped too — the same accepted residual class as a
    // lost radio frame, healed by the member's next head change or replug.
    if (!isRosterAuthority()) return;
    std::array<uint8_t, 6> member;
    std::array<uint8_t, 6> upstream;
    memcpy(member.data(), fromMac, 6);
    memcpy(upstream.data(), announce.upstreamMac, 6);
    const bool isNew = chainRoster.count(member) == 0;
    if (!isNew && chainRoster[member] == upstream) return;  // resend duplicate
    // Evict before the capacity check: a new member claiming an upstream a stale
    // claimant still occupies would otherwise be dropped at a full roster even
    // though eviction frees the slot. Re-read size after the eviction.
    evictStaleUpstreamClaimant(member, upstream);
    if (isNew && chainRoster.size() >= MAX_CHAIN_MEMBERS) {
        // The member cannot be told: its announce already drew SEND_SUCCESS, so
        // its HELLO confirmed bit reads 1 while it is absent from this roster
        // (see MAX_CHAIN_MEMBERS). Loud because the cap sits past any real chain
        // — reaching it means the roster is being fed by something else.
        LOG_E("RDC", "chain roster full (%u); dropping announce from %s, which "
                     "will still read as confirmed",
              MAX_CHAIN_MEMBERS, MacToString(fromMac));
        return;
    }
    chainRoster[member] = upstream;
    if (membershipChangeCallback) membershipChangeCallback();
}

void RemoteDeviceCoordinator::evictStaleUpstreamClaimant(
    const std::array<uint8_t, 6>& member, const std::array<uint8_t, 6>& upstream) {
    // Hardware invariant: one OUTPUT jack means one downstream, so at most one
    // member can hang off any upstream. A fresh announce claiming an upstream
    // some OTHER member already recorded proves the old claimant unplugged
    // (its departure report may still be in flight); drop it and its subtree.
    // Only announces evict — a head-transfer snapshot is older than any
    // announce and must not displace one.
    for (const auto& entry : chainRoster) {
        // entry.first == member guards self-eviction (wiping our own subtree);
        // the caller's duplicate check makes it unreachable today, kept so the
        // helper stays correct independent of caller ordering.
        if (entry.second != upstream || entry.first == member) continue;
        removeChainMemberSubtree(entry.first.data());
        return;
    }
}

bool RemoteDeviceCoordinator::isUpstreamHeldByOther(
    const std::array<uint8_t, 6>& upstream, const std::array<uint8_t, 6>& member) const {
    for (const auto& entry : chainRoster) {
        if (entry.second == upstream && entry.first != member) return true;
    }
    return false;
}

void RemoteDeviceCoordinator::onDisconnectReport(const uint8_t* fromMac,
                                                 const DisconnectReportPayload& report) {
    // Trust boundary: no report may name this device. A member that lost the
    // link to its own head sends nothing, so a self-referential report is
    // bogus — and honoring it would erase the whole roster via the subtree walk.
    if (memcmp(report.disconnectedMac, selfMac.data(), 6) == 0) {
        LOG_W("RDC", "dropping self-referential disconnect report");
        return;
    }
    if (!isRosterAuthority()) {
        // Head propagation is hop-by-hop, so a just-demoted head can still be
        // addressed under the old regime; forward the edit to the head now
        // held rather than dropping it.
        const uint64_t headMac48 = chainHeadState.load() & HEAD_MAC_MASK;
        // Never forward a report back to the device it arrived from: during a
        // head transient two mutually-stale non-authorities can each hold the
        // other as head and bounce the report between them, and with
        // KEEP_DISTINCT every hop would pile up a fresh pending retry entry.
        if (headMac48 == 0 || headMac48 == MacToUInt64(fromMac)) return;
        uint8_t headMac[6];
        unpackMac(headMac48, headMac);
        sendDisconnectReport(headMac, report.disconnectedMac);
        return;
    }
    removeChainMemberSubtree(report.disconnectedMac);
}

void RemoteDeviceCoordinator::onHeadTransfer(const uint8_t* /*fromMac*/,
                                             const HeadTransferPayload& transfer) {
    if (!isRosterAuthority()) return;
    if (transfer.memberCount > MAX_CHAIN_MEMBERS) {
        // Every sender caps at MAX_CHAIN_MEMBERS, so an oversized count means a
        // corrupt frame; the clamp bounds the array walk either way.
        LOG_W("RDC", "head transfer memberCount %u exceeds cap %u; clamping",
              (unsigned)transfer.memberCount, (unsigned)MAX_CHAIN_MEMBERS);
    }
    const uint8_t count =
        transfer.memberCount <= MAX_CHAIN_MEMBERS ? transfer.memberCount : MAX_CHAIN_MEMBERS;
    // Each pair carries the member's true recorded upstream, so the transferred
    // chain keeps its order and a mid-chain departure prunes exactly its tail.
    // A transfer snapshot is older than any announce, so it never displaces one:
    // it is skipped when the member already exists, or when its upstream slot is
    // already claimed by another member (a fresher announce that reached us
    // first). That skip, paired with the announce-side eviction, keeps the
    // one-downstream invariant true so a stale post-replug snapshot cannot fork.
    bool changed = false;
    for (uint8_t i = 0; i < count; ++i) {
        std::array<uint8_t, 6> member;
        std::array<uint8_t, 6> upstream;
        memcpy(member.data(), transfer.memberMacs[i], 6);
        memcpy(upstream.data(), transfer.upstreamMacs[i], 6);
        if (memcmp(member.data(), selfMac.data(), 6) == 0) continue;  // never roster self
        if (chainRoster.count(member) != 0) continue;
        if (isUpstreamHeldByOther(upstream, member)) continue;
        // Unlike the announce path this drop is self-healing: the head change that
        // triggered the transfer clears each member's confirmed bit and makes it
        // re-announce, so a member cut here comes back under its own announce.
        if (chainRoster.size() >= MAX_CHAIN_MEMBERS) break;
        chainRoster[member] = upstream;
        changed = true;
    }
    if (changed && membershipChangeCallback) membershipChangeCallback();
}
