#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include "device/serial-manager.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "device/device-type.hpp"

class Device;
class HandshakeApp;

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

    // ---- Chain-level surface (#154). Stubs until the connection-state
    // machines land (#155-#159): they return the standalone defaults so the
    // game layer can compile and wire against the final shape now. ----

    /// This device's chain role. STUB: always STANDALONE.
    virtual ChainRole getChainRole() const { return ChainRole::STANDALONE; }

    /// The chain head's MAC, or nullptr when this device is the head or
    /// standalone. STUB: always nullptr.
    virtual const uint8_t* getHeadMac() const { return nullptr; }

    /// Head-only chain member roster; empty for a child or standalone device.
    /// STUB: always empty.
    virtual std::vector<std::array<uint8_t, 6>> getChainMembers() const { return {}; }

    /// Registers the per-jack connect/disconnect observer.
    void setOnJackChange(JackChangeCallback callback) {
        jackChangeCallback = std::move(callback);
    }

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
};
