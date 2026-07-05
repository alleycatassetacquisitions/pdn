#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>

#include "device/drivers/serial-wrapper.hpp"
#include "device/device-type.hpp"

class WirelessManager;

struct DirectPeer {
    std::array<uint8_t, 6> macAddr;
    SerialIdentifier sid;
    DeviceType deviceType;
    uint16_t userId = 0xFFFF;  // 4-digit player code; 0xFFFF = not yet registered
};

// Tracks the direct cable peer on each jack and owns the ESP-NOW peer slot
// lifecycle for them: a peer's slot is registered when it first appears on
// either jack and released only when its MAC is absent from BOTH jacks — a
// two-device ring has the same MAC on both jacks, and releasing the slot on a
// one-cable disconnect would silently break wireless for the still-connected
// device. The 20-slot ESP-NOW table is finite; leaking one entry per neighbor
// that silent-dies over a multi-hour event eventually rejects new peers.
class DirectPeerTable {
public:
    /// Inert until initialize() provides the radio.
    DirectPeerTable();
    /// Nulls the injected radio pointer; the table owns nothing.
    ~DirectPeerTable();

    /// Injects the radio used for ESP-NOW slot registration; nullptr in unit
    /// tests disables slot management and the self-MAC check.
    void initialize(WirelessManager* wirelessManager);

    /// Fired on a direct-peer presence transition on a jack: previous=nullopt on
    /// connect, current=nullopt on disconnect. macPeers is already mutated when
    /// this runs (slot written on connect, erased on disconnect), so a handler
    /// re-querying getMacPeer observes the post-transition state. macPeers is the
    /// single source of truth for these edges; the RDC reacts rather than polling.
    using PeerChangeCallback =
        std::function<void(SerialIdentifier jack,
                           std::optional<DirectPeer> previous,
                           std::optional<DirectPeer> current)>;

    /// Registers the transition observer (see PeerChangeCallback).
    void setPeerChangeCallback(PeerChangeCallback cb) {
        peerChangeCallback = std::move(cb);
    }

    /// Registers a direct peer on a jack and its ESP-NOW slot (skipped when the
    /// same MAC is already live on the other jack). Returns false if the peer's
    /// MAC equals our own (self-loopback or spoofing); the peer is not stored
    /// and the caller must not adopt it. Fires the PeerChangeCallback as a
    /// connect only when the jack was previously empty; replacing a live jack's
    /// peer (an A->B swap with no intervening drop) surfaces as
    /// disconnect-then-connect.
    bool setMacPeer(SerialIdentifier jack, DirectPeer peer);

    /// Clears a jack's peer, fires the disconnect transition, and releases the
    /// ESP-NOW slot unless the same MAC is still live on the other jack.
    void removeMacPeer(SerialIdentifier jack);

    /// The peer on a jack, or nullptr when the jack is empty.
    const DirectPeer* getMacPeer(SerialIdentifier jack) const;

    /// True when this MAC is the live peer on either jack.
    bool isDirectPeer(const uint8_t* macAddress) const;

private:
    // Releases the ESP-NOW peer slot unless the MAC is still live on the
    // other jack (two-device ring shares one MAC on both jacks).
    void releaseSlotIfUnused(const uint8_t* macAddress);

    WirelessManager* wirelessManager = nullptr;

    std::map<SerialIdentifier, DirectPeer> macPeers;
    PeerChangeCallback peerChangeCallback;
};
