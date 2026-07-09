#include "device/remote-device-coordinator.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "apps/handshake/handshake.hpp"
#include "protocol-constants.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/serial-manager.hpp"
#include "device/drivers/logger.hpp"
#include "state/state-machine.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "wireless/mac-functions.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/hello-link-machine.hpp"

static constexpr std::array<SerialIdentifier, 3> HELLO_JACKS = {
    SerialIdentifier::OUTPUT_JACK,
    SerialIdentifier::INPUT_JACK,
    SerialIdentifier::INPUT_JACK_SECONDARY,
};

RemoteDeviceCoordinator::RemoteDeviceCoordinator() : handshakeWirelessManager(HandshakeWirelessManager()) {}

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
    if (helloConnectivityEnabled) {
        for (SerialIdentifier port : HELLO_JACKS) {
            HWSerialWrapper* hw = jackWrapper(port);
            if (hw != nullptr) hw->setByteCallback(nullptr);
            delete helloByPort_[portIndex(port)].machine;
            helloByPort_[portIndex(port)].machine = nullptr;
        }
    }
    delete inputPortHandshake;
    delete outputPortHandshake;
    delete secondaryInputPortHandshake;
}


void RemoteDeviceCoordinator::initialize(WirelessManager* wirelessManager, SerialManager* serialManager, Device* PDN) {
    this->serialManager = serialManager;
    this->wirelessManager_ = wirelessManager;
    if (PDN != nullptr) {
        selfDeviceType = PDN->getDeviceType();
    }

    handshakeWirelessManager.initialize(wirelessManager);

    inputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::INPUT_JACK);
    inputPortHandshake->initialize(PDN);

    // Only create the output port handshake when an output serial jack is present.
    // Devices like the FDN that have only input jacks will pass nullptr for outputJack.
    if (serialManager->getOutputJack() != nullptr) {
        outputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::OUTPUT_JACK);
        outputPortHandshake->initialize(PDN);
    }

    // Optionally initialize secondary input port (used by FDN which has two input jacks).
    if (serialManager->getSecondaryInputJack() != nullptr) {
        secondaryInputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::INPUT_JACK_SECONDARY);
        secondaryInputPortHandshake->initialize(PDN);
    }

    wirelessManager->setEspNowPacketHandler(
        PktType::kHandshakeCommand,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<HandshakeWirelessManager*>(ctx)->processHandshakeCommand(macAddress, data, dataLen);
        },
        &handshakeWirelessManager
    );

    announcementEmitCallback_ = [this](const uint8_t* toMac, uint8_t announcementId, const std::vector<std::array<uint8_t, 6>>& peers) {
        std::vector<uint8_t> buf;
        buf.push_back(announcementId);
        buf.push_back(static_cast<uint8_t>(peers.size()));
        for (const auto& mac : peers) {
            buf.insert(buf.end(), mac.begin(), mac.end());
        }
        wirelessManager_->sendEspNowData(toMac, PktType::kChainAnnouncement, buf.data(), buf.size());
    };

    wirelessManager->setEspNowPacketHandler(
        PktType::kChainAnnouncement,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<RemoteDeviceCoordinator*>(ctx)->processChainAnnouncementPacket(macAddress, data, dataLen);
        },
        this
    );

    wirelessManager->setEspNowPacketHandler(
        PktType::kChainAnnouncementAck,
        [](const uint8_t* macAddress, const uint8_t* data, const size_t dataLen, void* ctx) {
            static_cast<RemoteDeviceCoordinator*>(ctx)->processChainAnnouncementAckPacket(macAddress, data, dataLen);
        },
        this
    );
}

std::vector<SerialIdentifier> RemoteDeviceCoordinator::activePorts() const {
    std::vector<SerialIdentifier> ports;
    if (inputPortHandshake)          ports.push_back(SerialIdentifier::INPUT_JACK);
    if (outputPortHandshake)         ports.push_back(SerialIdentifier::OUTPUT_JACK);
    if (secondaryInputPortHandshake) ports.push_back(SerialIdentifier::INPUT_JACK_SECONDARY);
    return ports;
}

HandshakeApp* RemoteDeviceCoordinator::handshakeAppForPort(SerialIdentifier port) const {
    switch (port) {
        case SerialIdentifier::INPUT_JACK:           return inputPortHandshake;
        case SerialIdentifier::OUTPUT_JACK:          return outputPortHandshake;
        case SerialIdentifier::INPUT_JACK_SECONDARY: return secondaryInputPortHandshake;
        default:                                     return nullptr;
    }
}

void RemoteDeviceCoordinator::processChainAnnouncementAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen < 1) return;
    uint8_t ackedId = data[0];

    for (SerialIdentifier port : activePorts()) {
        const Peer* directPeer = handshakeWirelessManager.getMacPeer(port);
        if (directPeer == nullptr || memcmp(directPeer->macAddr.data(), fromMac, 6) != 0) continue;

        PendingAnnouncement& pending = pendingByPort_[portIndex(port)];
        if (pending.active && pending.announcementId == ackedId) {
            retryStats_.ackLatencyMsSum += pending.timer.getElapsedTime();
            retryStats_.ackCount++;
            pending.active = false;
            pending.timer.invalidate();
            return;
        }
    }
}

void RemoteDeviceCoordinator::processChainAnnouncementPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    // Wire format: [id(1)][count(1)][mac(6)]*count
    if (dataLen < 2) return;
    uint8_t announcementId = data[0];
    uint8_t peerCount = data[1];
    if (dataLen != 2 + (size_t)peerCount * 6) return;

    // Determine which port this sender is the direct peer of. Gate on the
    // handshake having reached CONNECTED — announcements during CONNECTING
    // would populate the daisy list while the port status is still
    // transitioning, violating the StatusMatchesPeerList invariant.
    SerialIdentifier port;
    bool found = false;
    for (SerialIdentifier candidate : activePorts()) {
        const Peer* directPeer = handshakeWirelessManager.getMacPeer(candidate);
        if (directPeer == nullptr) continue;
        if (memcmp(directPeer->macAddr.data(), fromMac, 6) != 0) continue;
        if (mapHandshakeStateToStatus(candidate) != PortStatus::CONNECTED) continue;
        port = candidate;
        found = true;
        break;
    }
    if (!found) return;

    std::vector<std::array<uint8_t, 6>> peers;
    for (uint8_t i = 0; i < peerCount; i++) {
        std::array<uint8_t, 6> mac;
        memcpy(mac.data(), data + 2 + i * 6, 6);
        peers.push_back(mac);
    }

    onChainAnnouncementReceived(fromMac, port, peers);

    wirelessManager_->sendEspNowData(fromMac, PktType::kChainAnnouncementAck, &announcementId, 1);
}

void RemoteDeviceCoordinator::sync(Device* PDN) {
    if (helloConnectivityEnabled) {
        // The handshake is quiesced on HELLO jacks: skipping its onStateLoop is
        // what keeps it off the UART (it neither consumes RX nor writes TX). The
        // chain-announcement machinery below rides handshake CONNECTED state, so
        // it stays dormant here too until HELLO drives its own peer identity (#157).
        for (SerialIdentifier port : HELLO_JACKS) {
            HelloLinkMachine* machine = helloByPort_[portIndex(port)].machine;
            if (machine) machine->onStateLoop(PDN);
        }
        return;
    }

    if (inputPortHandshake)          inputPortHandshake->onStateLoop(PDN);
    if (outputPortHandshake)         outputPortHandshake->onStateLoop(PDN);
    if (secondaryInputPortHandshake) secondaryInputPortHandshake->onStateLoop(PDN);

    // Wipe daisy-chained peers for any port whose direct peer has dropped.
    for (SerialIdentifier port : activePorts()) {
        if (handshakeWirelessManager.getMacPeer(port) == nullptr) {
            daisyChainedByPort_[portIndex(port)].clear();
        }
    }

    // Detect direct peer registration transitions and emit chain announcements.
    for (SerialIdentifier port : activePorts()) {
        const Peer* directPeer = handshakeWirelessManager.getMacPeer(port);
        auto& prev = previousDirectPeer_[portIndex(port)];
        bool nowPresent = (directPeer != nullptr);
        bool wasPresent = prev.has_value();

        if (!nowPresent && wasPresent) {
            // Notify the other ports about the disconnection
            for (SerialIdentifier otherPort : activePorts()) {
                if (otherPort == port) continue;
                const Peer* otherDirect = handshakeWirelessManager.getMacPeer(otherPort);
                if (otherDirect != nullptr) {
                    emitAnnouncementVia(otherPort, {});
                }
            }
            if (peerLostCallback_) {
                peerLostCallback_(prev->data());
            }
            notifyDisconnect();
        }

        if (nowPresent && !wasPresent) {
            registerPeer(directPeer->macAddr.data());
            // Notify all other ports about the new direct peer.
            for (SerialIdentifier otherPort : activePorts()) {
                if (otherPort == port) continue;
                const Peer* otherDirect = handshakeWirelessManager.getMacPeer(otherPort);
                if (otherDirect != nullptr) {
                    std::vector<std::array<uint8_t, 6>> forwardList;
                    forwardList.push_back(directPeer->macAddr);
                    for (const auto& mac : daisyChainedByPort_[portIndex(port)]) {
                        forwardList.push_back(mac);
                    }
                    emitAnnouncementVia(otherPort, forwardList);
                    emitAnnouncementVia(port, peersReachableVia(otherPort));
                }
            }
            notifyConnect();
        }

        prev = nowPresent ? std::optional<std::array<uint8_t, 6>>{directPeer->macAddr}
                          : std::nullopt;
    }

    // Retransmit any unacked pending announcements past the ack timeout.
    for (SerialIdentifier port : activePorts()) {
        PendingAnnouncement& pending = pendingByPort_[portIndex(port)];
        if (pending.active && pending.timer.expired()) {
            const Peer* directPeer = handshakeWirelessManager.getMacPeer(port);
            if (directPeer != nullptr && announcementEmitCallback_ && pending.retries < maxRetries_) {
                announcementEmitCallback_(directPeer->macAddr.data(), pending.announcementId, pending.peers);
                pending.retries++;
                pending.timer.setTimer(ackTimeoutMs_ << pending.retries);
                retryStats_.retries++;
            } else {
                if (pending.retries >= maxRetries_ && directPeer != nullptr) {
                    const uint8_t* t = directPeer->macAddr.data();
                    LOG_W("RDC",
                        "kChainAnnouncement abandoned after %u retries: target=%02X:%02X:%02X:%02X:%02X:%02X id=%u",
                        (unsigned)maxRetries_,
                        t[0], t[1], t[2], t[3], t[4], t[5],
                        (unsigned)pending.announcementId);
                    retryStats_.abandons++;
                }
                pending.active = false;
                pending.retries = 0;
                pending.peers.clear();
                pending.timer.invalidate();
            }
        }
    }
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
    // When HELLO owns the jack, connectivity comes from its link SM, not the
    // quiesced handshake.
    if (isHelloJack(port)) {
        return mapHelloLinkToStatus(port);
    }
    // A port is CONNECTED or it isn't; a daisy-chained peer does not upgrade its
    // status. Chain position is a device-level fact (getChainRole()).
    return mapHandshakeStateToStatus(port);
}

PortState RemoteDeviceCoordinator::getPortState(SerialIdentifier port) {
    std::vector<std::array<uint8_t, 6>> peerAddresses;

    const Peer* macPeer = handshakeWirelessManager.getMacPeer(port);

    if (macPeer != nullptr) {
        peerAddresses.push_back(macPeer->macAddr);
    }

    const auto& daisyChained = daisyChainedByPort_[portIndex(port)];
    for (const auto& mac : daisyChained) {
        peerAddresses.push_back(mac);
    }

    return PortState{ port, getPortStatus(port), peerAddresses };
}

void RemoteDeviceCoordinator::onChainAnnouncementReceived(
    const uint8_t* fromMac,
    SerialIdentifier port,
    const std::vector<std::array<uint8_t, 6>>& announcedPeers) {

    const Peer* directPeer = handshakeWirelessManager.getMacPeer(port);
    if (directPeer == nullptr || memcmp(directPeer->macAddr.data(), fromMac, 6) != 0) {
        return;
    }

    const uint8_t* selfMac = wirelessManager_ ? wirelessManager_->getMacAddress() : nullptr;

    std::vector<std::array<uint8_t, 6>> filtered;
    for (const auto& mac : announcedPeers) {
        if (memcmp(mac.data(), directPeer->macAddr.data(), 6) == 0) continue;
        if (selfMac && memcmp(mac.data(), selfMac, 6) == 0) continue;
        filtered.push_back(mac);
    }

    auto& chain = daisyChainedByPort_[portIndex(port)];
    if (filtered == chain) {
        return;
    }

    auto contains = [](const std::vector<std::array<uint8_t, 6>>& v,
                       const std::array<uint8_t, 6>& m) {
        for (const auto& e : v) if (e == m) return true;
        return false;
    };
    std::vector<std::array<uint8_t, 6>> toRemove;
    for (const auto& m : chain) if (!contains(filtered, m)) toRemove.push_back(m);
    for (const auto& m : toRemove) removeDaisyChainedPeer(port, m.data());
    for (const auto& m : filtered) if (!contains(chain, m)) addDaisyChainedPeer(port, m.data());

    notifyDaisyChained();

    // Forward propagation to all other ports with a direct peer.
    for (SerialIdentifier otherPort : activePorts()) {
        if (otherPort == port) continue;
        const Peer* otherDirect = handshakeWirelessManager.getMacPeer(otherPort);
        if (otherDirect != nullptr) {
            std::vector<std::array<uint8_t, 6>> forwardList;
            forwardList.push_back(directPeer->macAddr);
            for (const auto& mac : filtered) {
                forwardList.push_back(mac);
            }
            emitAnnouncementVia(otherPort, forwardList);
        }
    }
}

void RemoteDeviceCoordinator::emitAnnouncementVia(SerialIdentifier viaPort, const std::vector<std::array<uint8_t, 6>>& peers) {
    const Peer* directPeer = handshakeWirelessManager.getMacPeer(viaPort);
    if (directPeer == nullptr || !announcementEmitCallback_) return;

    uint8_t id = nextAnnouncementId_++;
    if (nextAnnouncementId_ == 0) nextAnnouncementId_ = 1;

    PendingAnnouncement& pending = pendingByPort_[portIndex(viaPort)];
    pending.active = true;
    pending.announcementId = id;
    pending.retries = 0;
    pending.peers = peers;
    pending.timer.setTimer(ackTimeoutMs_);
    retryStats_.sends++;

    announcementEmitCallback_(directPeer->macAddr.data(), id, peers);
}

std::vector<std::array<uint8_t, 6>> RemoteDeviceCoordinator::peersReachableVia(SerialIdentifier port) {
    std::vector<std::array<uint8_t, 6>> result;
    const Peer* direct = handshakeWirelessManager.getMacPeer(port);
    if (direct != nullptr) {
        result.push_back(direct->macAddr);
        for (const auto& mac : daisyChainedByPort_[portIndex(port)]) {
            result.push_back(mac);
        }
    }
    return result;
}

void RemoteDeviceCoordinator::setChainChangeCallback(std::function<void()> callback) {
    chainChangeCallback_ = callback;
}

void RemoteDeviceCoordinator::setPeerLostCallback(std::function<void(const uint8_t*)> callback) {
    peerLostCallback_ = callback;
}

void RemoteDeviceCoordinator::setAnnouncementEmitCallback(AnnouncementEmitCallback callback) {
    announcementEmitCallback_ = callback;
}

DeviceType RemoteDeviceCoordinator::getPeerDeviceType(SerialIdentifier port) const {
    const Peer* macPeer = handshakeWirelessManager.getMacPeer(port);
    return macPeer ? macPeer->deviceType : DeviceType::UNKNOWN;
}

PortStatus RemoteDeviceCoordinator::mapHandshakeStateToStatus(SerialIdentifier port) {
    HandshakeApp* app = handshakeAppForPort(port);

    if (!app || !app->getCurrentState()) {
        return PortStatus::DISCONNECTED;
    }

    int stateId = app->getCurrentState()->getStateId();

    switch (stateId) {
        case HandshakeStateId::OUTPUT_CONNECTED_STATE:
        case HandshakeStateId::INPUT_CONNECTED_STATE:
            return PortStatus::CONNECTED;

        case HandshakeStateId::OUTPUT_SEND_ID_STATE:
        case HandshakeStateId::INPUT_SEND_ID_STATE:
            return PortStatus::CONNECTING;

        case HandshakeStateId::OUTPUT_IDLE_STATE:
        case HandshakeStateId::INPUT_IDLE_STATE:
        default:
            if (handshakeWirelessManager.getMacPeer(port) != nullptr) {
                return PortStatus::CONNECTING;
            }
            return PortStatus::DISCONNECTED;
    }
}

const uint8_t* RemoteDeviceCoordinator::getPeerMac(SerialIdentifier port) const {
    const Peer* peer = handshakeWirelessManager.getMacPeer(port);
    return peer ? peer->macAddr.data() : nullptr;
}

bool RemoteDeviceCoordinator::isDirectPeer(const uint8_t* mac) const {
    if (!mac) return false;
    for (SerialIdentifier port : activePorts()) {
        const uint8_t* peer = getPeerMac(port);
        if (peer && memcmp(peer, mac, 6) == 0) return true;
    }
    return false;
}

bool RemoteDeviceCoordinator::canReachPeer(const uint8_t* mac) const {
    if (!mac) return false;
    if (isDirectPeer(mac)) return true;
    for (SerialIdentifier port : activePorts()) {
        for (const auto& daisy : daisyChainedByPort_[portIndex(port)]) {
            if (memcmp(daisy.data(), mac, 6) == 0) return true;
        }
    }
    return false;
}

void RemoteDeviceCoordinator::addDaisyChainedPeer(SerialIdentifier port, const uint8_t* macAddress) {
    std::array<uint8_t, 6> mac;
    memcpy(mac.data(), macAddress, 6);
    auto& chain = daisyChainedByPort_[portIndex(port)];
    for (const auto& existing : chain) {
        if (existing == mac) return;
    }
    if (chain.size() >= kMaxChainPeersPerPort) return;
    chain.push_back(mac);
    registerPeer(macAddress);
}

void RemoteDeviceCoordinator::removeDaisyChainedPeer(SerialIdentifier port, const uint8_t* macAddress) {
    auto& chain = daisyChainedByPort_[portIndex(port)];
    for (auto it = chain.begin(); it != chain.end(); ++it) {
        if (memcmp(it->data(), macAddress, 6) == 0) {
            chain.erase(it);
            // Only unregister if the MAC is not reachable via another port.
            for (SerialIdentifier otherPort : activePorts()) {
                if (otherPort == port) continue;
                for (const auto& m : daisyChainedByPort_[portIndex(otherPort)]) {
                    if (memcmp(m.data(), macAddress, 6) == 0) return;
                }
            }
            if (isDirectPeer(macAddress)) return;
            unregisterPeer(macAddress);
            return;
        }
    }
}

void RemoteDeviceCoordinator::registerPeer(const uint8_t* macAddress) {
    if (wirelessManager_ != nullptr) {
        wirelessManager_->addEspNowPeer(macAddress);
    }
}

void RemoteDeviceCoordinator::notifyConnect() {
    if (chainChangeCallback_) chainChangeCallback_();
}

void RemoteDeviceCoordinator::notifyDisconnect() {
    if (chainChangeCallback_) chainChangeCallback_();
}

void RemoteDeviceCoordinator::notifyDaisyChained() {
    if (chainChangeCallback_) chainChangeCallback_();
}

void RemoteDeviceCoordinator::unregisterPeer(const uint8_t* macAddress) {
    if (wirelessManager_ != nullptr) {
        wirelessManager_->removeEspNowPeer(macAddress);
    }
    for (SerialIdentifier port : activePorts()) {
        const Peer* peer = handshakeWirelessManager.getMacPeer(port);
        if (peer != nullptr && memcmp(peer->macAddr.data(), macAddress, 6) == 0) {
            handshakeWirelessManager.removeMacPeer(port);
        }
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

bool RemoteDeviceCoordinator::isHelloJack(SerialIdentifier port) const {
    return helloConnectivityEnabled && jackWrapper(port) != nullptr;
}

void RemoteDeviceCoordinator::enableHelloConnectivity() {
    if (helloConnectivityEnabled) return;
    helloConnectivityEnabled = true;

    if (wirelessManager_ != nullptr) {
        const uint8_t* mac = wirelessManager_->getMacAddress();
        if (mac != nullptr) memcpy(selfMac_.data(), mac, 6);
    }

    for (SerialIdentifier port : HELLO_JACKS) {
        HWSerialWrapper* hw = jackWrapper(port);
        if (hw == nullptr) continue;
        JackHelloLink& link = helloByPort_[portIndex(port)];
        link.parser.setHelloFrameHandler(
            [this, port](const HelloPayload& hello) { onHelloReceived(port, hello); });
        // exec() runs on the main loop, so the parser is fed single-threaded.
        hw->setByteCallback([this, port](uint8_t b) {
            helloByPort_[portIndex(port)].parser.feed(&b, 1);
        });

        HelloLinkContext context;
        context.jack = port;
        context.nowMs = [] { return RemoteDeviceCoordinator::nowMs(); };
        context.onContextRequest = [this](SerialIdentifier j) {
            if (contextRequestCallback) contextRequestCallback(j);
        };
        context.onJackChange = [this](SerialIdentifier j, bool connected) {
            if (jackChangeCallback) jackChangeCallback(j, connected);
        };
        context.resetParser = [this, port] {
            helloByPort_[portIndex(port)].parser.requestReset();
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
    if (!helloConnectivityEnabled) return;

    HelloPayload hello{};
    memcpy(hello.source, selfMac_.data(), 6);
    hello.deviceType = static_cast<uint8_t>(selfDeviceType);
    // headMac stays zero and confirmed stays 0 until the device chain SM (#156)
    // learns the head and completes its context exchange.
    hello.confirmed = 0;

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
    if (memcmp(hello.source, selfMac_.data(), 6) == 0) return;

    HelloLinkMachine* machine = helloByPort_[portIndex(jack)].machine;
    if (machine) machine->onHelloReceived(hello);
}

void RemoteDeviceCoordinator::onContextExchangeComplete(SerialIdentifier jack) {
    HelloLinkMachine* machine = helloByPort_[portIndex(jack)].machine;
    if (machine) machine->onContextExchangeComplete();
}

RemoteDeviceCoordinator::HelloLinkState
RemoteDeviceCoordinator::getHelloLinkState(SerialIdentifier jack) const {
    const HelloLinkMachine* machine = helloByPort_[portIndex(jack)].machine;
    if (machine == nullptr) return HelloLinkState::IDLE;
    switch (machine->currentStateId()) {
        case HELLO_LINK_CONNECTED:  return HelloLinkState::CONNECTED;
        case HELLO_LINK_CONNECTING: return HelloLinkState::CONNECTING;
        default:                    return HelloLinkState::IDLE;
    }
}

PortStatus RemoteDeviceCoordinator::mapHelloLinkToStatus(SerialIdentifier port) const {
    const HelloLinkMachine* machine = helloByPort_[portIndex(port)].machine;
    if (machine == nullptr) return PortStatus::DISCONNECTED;
    switch (machine->currentStateId()) {
        case HELLO_LINK_CONNECTED:  return PortStatus::CONNECTED;
        case HELLO_LINK_CONNECTING: return PortStatus::CONNECTING;
        default:                    return PortStatus::DISCONNECTED;
    }
}
