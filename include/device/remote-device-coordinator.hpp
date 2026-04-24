#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include "device/serial-manager.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/handshake-wireless-manager.hpp"
#include "device/device-type.hpp"

class Device;
class HandshakeApp;

enum class PortStatus {
    DISCONNECTED = 0, // No Connection. Handshake is in Idle state.
    CONNECTING = 1, // Port is NOT connected && NOT in handshake idle state.
    CONNECTED = 2, // Port is connected to exactly 1 peer.
    DAISY_CHAINED = 3, // Port is connected to more than 1 peer.
};

    struct PortState {
        SerialIdentifier port;
        PortStatus status;
        std::vector<std::array<uint8_t, 6>> peerMacAddresses;
};

class RemoteDeviceCoordinator {
public:
    RemoteDeviceCoordinator();
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

    virtual PortStatus getPortStatus(SerialIdentifier port);
    PortState getPortState(SerialIdentifier port);

    /**
     * Returns a pointer to the peer's MAC address for the given port, or nullptr if no peer is connected.
     * Prefer this over getPortState() when only the MAC address is needed.
     */
    virtual const uint8_t* getPeerMac(SerialIdentifier port) const;

    virtual DeviceType getPeerDeviceType(SerialIdentifier port) const;

    // Returns true iff `mac` matches the direct peer on either jack.
    virtual bool isDirectPeer(const uint8_t* mac) const;

    // Reachable via either jack (direct peer or daisy-chained).
    virtual bool canReachPeer(const uint8_t* mac) const;

    /**
     * Called when a chain announcement is received from a direct peer.
     * Replaces the port's daisy-chained peer list with the announced list,
     * filtering out self-MAC and the direct peer.
     */
    void onChainAnnouncementReceived(
        const uint8_t* fromMac,
        SerialIdentifier port,
        const std::vector<std::array<uint8_t, 6>>& announcedPeers);

    void setChainChangeCallback(std::function<void()> callback);

    // Direct peer drops only; daisy-chained drops arrive via chain announcements.
    void setPeerLostCallback(std::function<void(const uint8_t*)> callback);

    void processChainAnnouncementPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);

    using AnnouncementEmitCallback = std::function<void(const uint8_t* toMac, uint8_t announcementId, const std::vector<std::array<uint8_t, 6>>& peers)>;
    void setAnnouncementEmitCallback(AnnouncementEmitCallback callback);

    void processChainAnnouncementAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);

    void registerPeer(const uint8_t* macAddress);
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
    RetryStats getRetryStats() const { return retryStats_; }

private:
    RetryStats retryStats_;
    std::array<std::vector<std::array<uint8_t, 6>>, 2> daisyChainedByPort_;
    std::array<std::optional<std::array<uint8_t, 6>>, 2> previousDirectPeer_;
    uint8_t nextAnnouncementId_ = 1;

    struct PendingAnnouncement {
        bool active = false;
        uint8_t announcementId = 0;
        uint8_t retries = 0;
        std::vector<std::array<uint8_t, 6>> peers;
        SimpleTimer timer;
    };
    std::array<PendingAnnouncement, 2> pendingByPort_;
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

    HandshakeWirelessManager handshakeWirelessManager;

    HandshakeApp* inputPortHandshake = nullptr;
    HandshakeApp* outputPortHandshake = nullptr;
};
