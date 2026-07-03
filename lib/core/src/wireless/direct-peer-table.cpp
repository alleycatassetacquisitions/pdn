#include "wireless/direct-peer-table.hpp"

#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/mac-functions.hpp"

namespace {
constexpr const char* DPT_TAG = "DPT";
}

DirectPeerTable::DirectPeerTable() {}

DirectPeerTable::~DirectPeerTable() {
    wirelessManager = nullptr;
}

void DirectPeerTable::initialize(WirelessManager* wirelessManager) {
    this->wirelessManager = wirelessManager;
}

bool DirectPeerTable::setMacPeer(SerialIdentifier jack, DirectPeer peer) {
    // Reject self-MAC: a peer claiming our own MAC (self-loopback or a neighbor
    // spoofing it) must never register as a direct peer; violates spec
    // invariant DirectPeerIsNeverSelf.
    if (wirelessManager != nullptr) {
        const uint8_t* selfMac = wirelessManager->getMacAddress();
        if (selfMac != nullptr && memcmp(peer.macAddr.data(), selfMac, 6) == 0) {
            return false;
        }
    }
    std::map<SerialIdentifier, DirectPeer>::iterator it = macPeers.find(jack);
    const bool wasEmpty = (it == macPeers.end());
    const bool changed = (wasEmpty || it->second.macAddr != peer.macAddr);
    if (changed) {
        LOG_W(DPT_TAG, "setMacPeer jack=%c mac=%s sid=%d type=%d",
              jack == SerialIdentifier::INPUT_JACK ? 'I' : 'O',
              MacToString(peer.macAddr.data()),
              (int)peer.sid, (int)peer.deviceType);
    }
    // A different MAC on an occupied jack is a swap (old peer left, new one
    // plugged in within the silent-link window): surface it as
    // disconnect-then-connect so subscribers never see the dying entry, and
    // release the old peer's ESP-NOW slot exactly as a real disconnect would.
    if (changed && !wasEmpty) {
        DirectPeer previous = it->second;
        macPeers.erase(it);
        if (peerChangeCallback) {
            peerChangeCallback(jack, previous, std::nullopt);
        }
        releaseSlotIfUnused(previous.macAddr.data());
    }
    // Register the ESP-NOW slot before storing: isDirectPeer still reflects the
    // pre-connect state here, so a MAC already live on the other jack (the
    // two-device ring) is not double-registered.
    if (changed && wirelessManager != nullptr && !isDirectPeer(peer.macAddr.data())) {
        wirelessManager->addEspNowPeer(peer.macAddr.data());
    }
    macPeers[jack] = peer;
    if (changed && peerChangeCallback) {
        peerChangeCallback(jack, std::nullopt, peer);
    }
    return true;
}

void DirectPeerTable::removeMacPeer(SerialIdentifier jack) {
    std::map<SerialIdentifier, DirectPeer>::iterator it = macPeers.find(jack);
    if (it == macPeers.end()) return;
    DirectPeer previous = it->second;
    macPeers.erase(it);
    if (peerChangeCallback) {
        peerChangeCallback(jack, previous, std::nullopt);
    }
    releaseSlotIfUnused(previous.macAddr.data());
}

const DirectPeer* DirectPeerTable::getMacPeer(SerialIdentifier jack) const {
    std::map<SerialIdentifier, DirectPeer>::const_iterator it = macPeers.find(jack);
    if (it == macPeers.end()) return nullptr;
    return &it->second;
}

bool DirectPeerTable::isDirectPeer(const uint8_t* macAddress) const {
    if (macAddress == nullptr) return false;
    for (const std::pair<const SerialIdentifier, DirectPeer>& entry : macPeers) {
        if (memcmp(entry.second.macAddr.data(), macAddress, 6) == 0) return true;
    }
    return false;
}

void DirectPeerTable::releaseSlotIfUnused(const uint8_t* macAddress) {
    // Only release the ESP-NOW slot when the MAC is gone from BOTH jacks: a
    // two-device ring carries the same neighbor on both, and dropping one
    // cable must not break the radio path the other still uses.
    if (wirelessManager == nullptr) return;
    if (isDirectPeer(macAddress)) return;
    wirelessManager->removeEspNowPeer(macAddress);
}
