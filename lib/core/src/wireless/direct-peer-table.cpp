//
// Created by Elli Furedy on 2/22/2025.
//
#include <cstring>
#include "wireless/direct-peer-table.hpp"
#include "wireless/mac-functions.hpp"

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
    auto it = macPeers.find(jack);
    const bool wasEmpty = (it == macPeers.end());
    const bool changed = (wasEmpty || it->second.macAddr != peer.macAddr);
    if (changed) {
        LOG_W("DPT", "setMacPeer jack=%c mac=%s sid=%d type=%d",
              jack == SerialIdentifier::INPUT_JACK ? 'I' : 'O',
              MacToString(peer.macAddr.data()),
              (int)peer.sid, (int)peer.deviceType);
    }
    // A different MAC on an occupied jack is a swap (old peer left, new one
    // plugged in within the silent-link window): surface it as
    // disconnect-then-connect so subscribers release the old ESP-NOW slot.
    // The slot is cleared before the disconnect callback fires; the handler
    // probes isDirectPeer() and must not see the dying entry.
    if (changed && !wasEmpty && peerChangeCallback_) {
        DirectPeer previous = it->second;
        macPeers.erase(it);
        peerChangeCallback_(jack, previous, std::nullopt);
    }
    macPeers[jack] = peer;
    if (changed && peerChangeCallback_) {
        peerChangeCallback_(jack, std::nullopt, peer);
    }
    return true;
}

void DirectPeerTable::removeMacPeer(SerialIdentifier jack) {
    auto it = macPeers.find(jack);
    if (it == macPeers.end()) return;
    DirectPeer previous = it->second;
    macPeers.erase(it);
    if (peerChangeCallback_) {
        peerChangeCallback_(jack, previous, std::nullopt);
    }
}

const DirectPeer* DirectPeerTable::getMacPeer(SerialIdentifier jack) const {
    auto it = macPeers.find(jack);
    if (it == macPeers.end()) return nullptr;
    return &it->second;
}
