#include "device/remote-device-coordinator.hpp"
#include "apps/handshake/handshake-states.hpp"
#include "apps/handshake/handshake.hpp"
#include "device/device-constants.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "device/serial-manager.hpp"
#include "device/drivers/logger.hpp"
#include "state/state-machine.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "wireless/peer-comms-types.hpp"

RemoteDeviceCoordinator::RemoteDeviceCoordinator() : handshakeWirelessManager(HandshakeWirelessManager()) {}

RemoteDeviceCoordinator::~RemoteDeviceCoordinator() {
    delete inputPortHandshake;
    delete outputPortHandshake;
}


void RemoteDeviceCoordinator::initialize(WirelessManager* wirelessManager, SerialManager* serialManager, Device* PDN) {
    this->serialManager = serialManager;
    this->wirelessManager_ = wirelessManager;

    handshakeWirelessManager.initialize(wirelessManager);

    inputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::INPUT_JACK);
    outputPortHandshake = new HandshakeApp(&handshakeWirelessManager, SerialIdentifier::OUTPUT_JACK);

    inputPortHandshake->initialize(PDN);
    outputPortHandshake->initialize(PDN);

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

void RemoteDeviceCoordinator::processChainAnnouncementAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
    if (dataLen < 1) return;
    uint8_t ackedId = data[0];

    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
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
    for (SerialIdentifier candidate : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
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
    inputPortHandshake->onStateLoop(PDN);
    outputPortHandshake->onStateLoop(PDN);

    // Wipe daisy-chained peers for any port whose direct peer has dropped.
    // The daisy-chained list represents peers reachable through the direct peer,
    // so without a direct peer they are no longer reachable.
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        if (handshakeWirelessManager.getMacPeer(port) == nullptr) {
            daisyChainedByPort_[portIndex(port)].clear();
        }
    }

    // Detect direct peer registration transitions and emit chain announcements.
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const Peer* directPeer = handshakeWirelessManager.getMacPeer(port);
        bool nowPresent = (directPeer != nullptr);
        bool wasPresent = previousDirectPeerPresent_[portIndex(port)];

        if (!nowPresent && wasPresent) {
            SerialIdentifier otherPort = (port == SerialIdentifier::INPUT_JACK)
                ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
            const Peer* otherDirect = handshakeWirelessManager.getMacPeer(otherPort);
            if (otherDirect != nullptr) {
                // Forward: tell other side that nothing is reachable through changed_port
                emitAnnouncementVia(otherPort, {});
            }
            notifyDisconnect();
        }

        if (nowPresent && !wasPresent) {
            registerPeer(directPeer->macAddr.data());
            SerialIdentifier otherPort = (port == SerialIdentifier::INPUT_JACK)
                ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
            const Peer* otherDirect = handshakeWirelessManager.getMacPeer(otherPort);

            // Forward: tell other side about new direct peer + its chain.
            // Only possible if the other side has a direct peer to send to.
            if (otherDirect != nullptr) {
                std::vector<std::array<uint8_t, 6>> forwardList;
                forwardList.push_back(directPeer->macAddr);
                for (const auto& mac : daisyChainedByPort_[portIndex(port)]) {
                    forwardList.push_back(mac);
                }
                emitAnnouncementVia(otherPort, forwardList);
            }

            emitAnnouncementVia(port, peersReachableVia(otherPort));
            notifyConnect();
        }

        previousDirectPeerPresent_[portIndex(port)] = nowPresent;
    }

    // Retransmit any unacked pending announcements past the ack timeout.
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        PendingAnnouncement& pending = pendingByPort_[portIndex(port)];
        if (pending.active && pending.timer.expired()) {
            const Peer* directPeer = handshakeWirelessManager.getMacPeer(port);
            if (directPeer != nullptr && announcementEmitCallback_ && pending.retries < maxRetries_) {
                announcementEmitCallback_(directPeer->macAddr.data(), pending.announcementId, pending.peers);
                pending.retries++;
                // Exponential backoff between retransmits: 200, 400, 800ms
                // (maxRetries_=3). Gives the async driver queue time to drain
                // between retries instead of layering new sends on top of
                // in-flight ones.
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
    return port == SerialIdentifier::INPUT_JACK ? 0 : 1;
}

PortStatus RemoteDeviceCoordinator::getPortStatus(SerialIdentifier port) {
    PortStatus statusByState = mapHandshakeStateToStatus(port);

    if (statusByState == PortStatus::CONNECTED && !daisyChainedByPort_[portIndex(port)].empty()) {
        return PortStatus::DAISY_CHAINED;
    }

    return statusByState;
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

    // Apply the change as a diff against the current chain so all daisy
    // mutations route through addDaisyChainedPeer / removeDaisyChainedPeer.
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

    // Forward propagation: tell the other side's direct peer about what's reachable
    // through us via this port (direct peer + filtered daisy peers).
    SerialIdentifier otherPort = (port == SerialIdentifier::INPUT_JACK)
        ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
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

void RemoteDeviceCoordinator::emitAnnouncementVia(SerialIdentifier viaPort, const std::vector<std::array<uint8_t, 6>>& peers) {
    const Peer* directPeer = handshakeWirelessManager.getMacPeer(viaPort);
    if (directPeer == nullptr || !announcementEmitCallback_) return;

    uint8_t id = nextAnnouncementId_++;
    if (nextAnnouncementId_ == 0) nextAnnouncementId_ = 1;  // skip 0 sentinel

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

void RemoteDeviceCoordinator::setAnnouncementEmitCallback(AnnouncementEmitCallback callback) {
    announcementEmitCallback_ = callback;
}

DeviceType RemoteDeviceCoordinator::getPeerDeviceType(SerialIdentifier port) const {
    const Peer* macPeer = handshakeWirelessManager.getMacPeer(port);
    return macPeer ? macPeer->deviceType : DeviceType::UNKNOWN;
}

PortStatus RemoteDeviceCoordinator::mapHandshakeStateToStatus(SerialIdentifier port) {
    HandshakeApp* app = (port == SerialIdentifier::INPUT_JACK) ? inputPortHandshake : outputPortHandshake;

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
            // Transient: if HWM has already registered a peer but the state
            // machine hasn't yet committed the transition out of IDLE (e.g.,
            // between stringCallback and the next sync()), report CONNECTING
            // so the StatusMatchesPeerList invariant holds: getPeerMac() is
            // never non-null while status == DISCONNECTED.
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
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const uint8_t* peer = getPeerMac(port);
        if (peer && memcmp(peer, mac, 6) == 0) return true;
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
    // Hard cap to stay within the ESP-NOW peer-table limit (20 on ESP32-S3).
    if (chain.size() >= kMaxChainPeersPerPort) return;
    chain.push_back(mac);
    // Register with the ESP-NOW peer table so the champion can unicast game
    // events to this supporter. Idempotent at the driver level.
    registerPeer(macAddress);
}

void RemoteDeviceCoordinator::removeDaisyChainedPeer(SerialIdentifier port, const uint8_t* macAddress) {
    auto& chain = daisyChainedByPort_[portIndex(port)];
    for (auto it = chain.begin(); it != chain.end(); ++it) {
        if (memcmp(it->data(), macAddress, 6) == 0) {
            chain.erase(it);
            // Free the ESP-NOW peer slot unless the same MAC is still
            // daisy-chained on the OTHER port (ring-topology remnant).
            SerialIdentifier otherPort = (port == SerialIdentifier::INPUT_JACK)
                ? SerialIdentifier::OUTPUT_JACK : SerialIdentifier::INPUT_JACK;
            for (const auto& m : daisyChainedByPort_[portIndex(otherPort)]) {
                if (memcmp(m.data(), macAddress, 6) == 0) return;
            }
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
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK}) {
        const Peer* peer = handshakeWirelessManager.getMacPeer(port);
        if (peer != nullptr && memcmp(peer->macAddr.data(), macAddress, 6) == 0) {
            handshakeWirelessManager.removeMacPeer(port);
        }
    }
}

