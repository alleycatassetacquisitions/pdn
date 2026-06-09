#pragma once

//
// Created by Elli Furedy on 2/22/2025.
//
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include "device/drivers/serial-wrapper.hpp"
#include "game/player.hpp"
#include "mac-functions.hpp"
#include "device/wireless-manager.hpp"
#include "device/serial-manager.hpp"
#include "device/device-type.hpp"

struct DirectPeer {
    std::array<uint8_t, 6> macAddr;
    SerialIdentifier sid;
    DeviceType deviceType;
    uint16_t userId = 0xFFFF;  // 4-digit player code; 0xFFFF = not yet registered
};

class DirectPeerTable {
public:
    DirectPeerTable();
    ~DirectPeerTable();

    void initialize(WirelessManager* wirelessManager);

    // Fired on a direct-peer presence transition on a jack: previous=nullopt on
    // connect, current=nullopt on disconnect. macPeers is already mutated when
    // this runs (slot written on connect, erased on disconnect), so a handler
    // re-querying getMacPeer observes the post-transition state. macPeers is the
    // single source of truth for these edges; the RDC reacts rather than polling.
    using PeerChangeCallback =
        std::function<void(SerialIdentifier jack,
                           std::optional<DirectPeer> previous,
                           std::optional<DirectPeer> current)>;
    void setPeerChangeCallback(PeerChangeCallback cb) {
        peerChangeCallback_ = std::move(cb);
    }

    // Registers a direct peer on a jack. Returns false if the peer's MAC
    // equals our own (self-loopback or spoofing); the peer is not stored
    // and the caller must not adopt the peer. Fires the
    // PeerChangeCallback as a connect only when the jack was previously empty;
    // replacing a live jack's peer (an A->B swap with no intervening drop)
    // updates the slot silently.
    bool setMacPeer(SerialIdentifier jack, DirectPeer peer);

    void removeMacPeer(SerialIdentifier jack);

    const DirectPeer* getMacPeer(SerialIdentifier jack) const;

private:
    WirelessManager* wirelessManager;

    std::map<SerialIdentifier, DirectPeer> macPeers;
    PeerChangeCallback peerChangeCallback_;
};
