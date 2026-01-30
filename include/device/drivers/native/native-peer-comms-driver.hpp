#pragma once

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include <map>
#include <cstring>

class NativePeerCommsDriver : public PeerCommsDriverInterface {
public:
    explicit NativePeerCommsDriver(const std::string& name) : PeerCommsDriverInterface(name) {
        // Get unique MAC from broker
        NativePeerBroker::getInstance().generateUniqueMac(macAddress_);
    }

    ~NativePeerCommsDriver() override {
        NativePeerBroker::getInstance().unregisterPeer(macAddress_);
    }

    int initialize() override {
        // Register with the broker
        NativePeerBroker::getInstance().registerPeer(this, macAddress_);
        return 0;
    }

    void exec() override {
        // Packet delivery handled by broker calling receivePacket directly
    }

    void connect() override {
        // In native simulation, connect is instant
        peerCommsState_ = PeerCommsState::CONNECTED;
    }

    void disconnect() override {
        peerCommsState_ = PeerCommsState::DISCONNECTED;
    }

    PeerCommsState getPeerCommsState() override {
        return peerCommsState_;
    }

    void setPeerCommsState(PeerCommsState state) override {
        if (state == PeerCommsState::CONNECTED && peerCommsState_ != PeerCommsState::CONNECTED) {
            connect();
        } else if (state == PeerCommsState::DISCONNECTED && peerCommsState_ != PeerCommsState::DISCONNECTED) {
            disconnect();
        }
    }

    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) override {
        if (peerCommsState_ != PeerCommsState::CONNECTED) {
            return -1;  // Cannot send when disconnected
        }
        NativePeerBroker::getInstance().sendPacket(macAddress_, dst, packetType, data, length);
        return 0; // Success
    }

    void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) override {
        handlers_[packetType] = {callback, ctx};
    }

    void clearPacketHandler(PktType packetType) override {
        handlers_.erase(packetType);
    }

    const uint8_t* getGlobalBroadcastAddress() override {
        return NativePeerBroker::getInstance().getBroadcastAddress();
    }

    uint8_t* getMacAddress() override {
        return macAddress_;
    }

    void removePeer(uint8_t* macAddr) override {
        // In native simulation, peers are managed by broker
        // This is a no-op but could track "known peers" if needed
    }

    /**
     * Called by the broker to deliver a packet to this peer.
     */
    void receivePacket(const uint8_t* srcMac, PktType packetType, 
                       const uint8_t* data, size_t length) {
        // Only process packets when connected
        if (peerCommsState_ != PeerCommsState::CONNECTED) {
            return;
        }
        
        auto it = handlers_.find(packetType);
        if (it != handlers_.end()) {
            it->second.callback(srcMac, data, length, it->second.context);
        }
    }

    /**
     * Get MAC address as string for display.
     */
    std::string getMacString() const {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 macAddress_[0], macAddress_[1], macAddress_[2],
                 macAddress_[3], macAddress_[4], macAddress_[5]);
        return std::string(buf);
    }

private:
    struct HandlerEntry {
        PacketCallback callback;
        void* context;
    };

    std::map<PktType, HandlerEntry> handlers_;
    uint8_t macAddress_[6];
    PeerCommsState peerCommsState_ = PeerCommsState::DISCONNECTED;
};

// Implementation of broker's deliverPackets that depends on NativePeerCommsDriver
inline void NativePeerBroker::deliverPackets() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    while (!pendingPackets_.empty()) {
        PeerPacket& packet = pendingPackets_.front();
        
        if (packet.isBroadcast) {
            // Deliver to all peers except sender
            for (auto& [mac, peer] : peers_) {
                if (!macEquals(mac, packet.srcMac.data())) {
                    peer->receivePacket(packet.srcMac.data(), packet.packetType,
                                       packet.data.data(), packet.data.size());
                }
            }
        } else {
            // Deliver to specific peer
            auto it = peers_.find(packet.dstMac);
            if (it != peers_.end()) {
                it->second->receivePacket(packet.srcMac.data(), packet.packetType,
                                         packet.data.data(), packet.data.size());
            }
        }
        
        pendingPackets_.pop();
    }
}
