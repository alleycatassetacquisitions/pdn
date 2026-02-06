#pragma once

#include <vector>
#include <queue>
#include <map>
#include <array>
#include <cstring>
#include <functional>
#include <mutex>
#include "wireless/peer-comms-types.hpp"

// Forward declaration
class NativePeerCommsDriver;

// Packet structure for queued messages
struct PeerPacket {
    std::array<uint8_t, 6> srcMac;
    std::array<uint8_t, 6> dstMac;
    PktType packetType;
    std::vector<uint8_t> data;
    bool isBroadcast;
};

/**
 * Singleton broker that routes packets between NativePeerCommsDriver instances.
 * Simulates ESP-NOW communication for native builds.
 */
class NativePeerBroker {
public:
    static NativePeerBroker& getInstance() {
        static NativePeerBroker instance;
        return instance;
    }

    // Prevent copying
    NativePeerBroker(const NativePeerBroker&) = delete;
    NativePeerBroker& operator=(const NativePeerBroker&) = delete;

    /**
     * Register a peer comms driver with its MAC address.
     * Called when a NativePeerCommsDriver is initialized.
     */
    void registerPeer(NativePeerCommsDriver* peer, const uint8_t* macAddress) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::array<uint8_t, 6> mac;
        std::memcpy(mac.data(), macAddress, 6);
        peers_[mac] = peer;
    }

    /**
     * Unregister a peer comms driver.
     * Called when a NativePeerCommsDriver is destroyed.
     */
    void unregisterPeer(const uint8_t* macAddress) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::array<uint8_t, 6> mac;
        std::memcpy(mac.data(), macAddress, 6);
        peers_.erase(mac);
    }

    /**
     * Queue a packet for delivery.
     * If dstMac is the broadcast address, packet is delivered to all peers except sender.
     */
    void sendPacket(const uint8_t* srcMac, const uint8_t* dstMac, 
                    PktType packetType, const uint8_t* data, size_t length) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PeerPacket packet;
        std::memcpy(packet.srcMac.data(), srcMac, 6);
        std::memcpy(packet.dstMac.data(), dstMac, 6);
        packet.packetType = packetType;
        packet.data.assign(data, data + length);
        packet.isBroadcast = isBroadcastAddress(dstMac);
        
        pendingPackets_.push(packet);
    }

    /**
     * Deliver pending packets to registered peers.
     * Should be called from the main loop to process queued messages.
     */
    void deliverPackets();

    /**
     * Get the global broadcast address.
     */
    const uint8_t* getBroadcastAddress() const {
        return broadcastAddress_;
    }

    /**
     * Generate a unique MAC address for a new device.
     */
    void generateUniqueMac(uint8_t* macOut) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Generate locally administered unicast MAC
        macOut[0] = 0x02; // Locally administered bit set
        macOut[1] = 0x00;
        macOut[2] = 0x00;
        macOut[3] = 0x00;
        macOut[4] = (nextMacId_ >> 8) & 0xFF;
        macOut[5] = nextMacId_ & 0xFF;
        nextMacId_++;
    }

    /**
     * Get count of registered peers.
     */
    size_t getPeerCount() const {
        return peers_.size();
    }
    
    /**
     * Get count of pending packets waiting to be delivered.
     */
    size_t getPendingPacketCount() const {
        return pendingPackets_.size();
    }
    
    /**
     * Check if a MAC address is registered.
     */
    bool isPeerRegistered(const uint8_t* macAddress) const {
        std::array<uint8_t, 6> mac;
        std::memcpy(mac.data(), macAddress, 6);
        return peers_.find(mac) != peers_.end();
    }

private:
    NativePeerBroker() : nextMacId_(1) {
        // Initialize broadcast address (all 0xFF)
        std::memset(broadcastAddress_, 0xFF, 6);
    }

    bool isBroadcastAddress(const uint8_t* mac) const {
        for (int i = 0; i < 6; i++) {
            if (mac[i] != 0xFF) return false;
        }
        return true;
    }

    bool macEquals(const std::array<uint8_t, 6>& a, const uint8_t* b) const {
        return std::memcmp(a.data(), b, 6) == 0;
    }

    std::map<std::array<uint8_t, 6>, NativePeerCommsDriver*> peers_;
    std::queue<PeerPacket> pendingPackets_;
    uint8_t broadcastAddress_[6];
    uint16_t nextMacId_;
    std::mutex mutex_;
};
