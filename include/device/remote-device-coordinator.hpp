#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include "device/serial-manager.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/handshake-wireless-manager.hpp"

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
    const uint8_t* getPeerMac(SerialIdentifier port) const;

    /**
     * Register a handler for messages arriving on the given jack whose content begins with prefix.
     * Multiple handlers may be registered on the same jack with different prefixes.
     */
    virtual void registerSerialHandler(const std::string& prefix, SerialIdentifier jack,
                                       std::function<void(const std::string&)> handler);

    /**
     * Remove the handler previously registered for (prefix, jack).
     */
    virtual void unregisterSerialHandler(const std::string& prefix, SerialIdentifier jack);

private:
    void routeSerialMessage(const std::string& msg, SerialIdentifier jack);

    void notifyDisconnect();
    void notifyConnect();
    void notifyDaisyChained();

    /**
     * handshake idle state - disconnected
     * handshake send id state - connecting
     * handshake connected state - connected
     */
    PortStatus mapHandshakeStateToStatus(SerialIdentifier port);

    void addDaisyChainedPeer(const uint8_t* macAddress);
    void removeDaisyChainedPeer(const uint8_t* macAddress);

    void registerPeer(const uint8_t* macAddress);
    void unregisterPeer(const uint8_t* macAddress);

    SerialManager* serialManager = nullptr;

    std::map<std::string, std::function<void(const std::string&)>> outputSerialHandlers_;
    std::map<std::string, std::function<void(const std::string&)>> inputSerialHandlers_;

    HandshakeWirelessManager handshakeWirelessManager;

    HandshakeApp* inputPortHandshake = nullptr;
    HandshakeApp* outputPortHandshake = nullptr;

    SimpleTimer syncLogTimer;
};
