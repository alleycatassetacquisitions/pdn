#pragma once

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/native/native-peer-broker.hpp"
#include <map>
#include <deque>
#include <mutex>
#include <queue>
#include <vector>
#include <cstring>

// Track packet history for CLI display
struct PacketHistoryEntry {
    bool isSent;  // true = sent, false = received
    std::string srcMac;
    std::string dstMac;
    PktType packetType;
    size_t length;
};

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
        std::queue<DeferredPacket> pending;
        {
            std::lock_guard<std::mutex> lock(recvMutex_);
            std::swap(pending, recvQueue_);
        }

        while (!pending.empty()) {
            DeferredPacket& pkt = pending.front();
            PacketHistoryEntry entry;
            entry.isSent = false;
            entry.srcMac = macToString(pkt.srcMac);
            entry.dstMac = getMacString();
            entry.packetType = pkt.type;
            entry.length = pkt.data.size();
            addToHistory(entry);

            auto it = handlers_.find(pkt.type);
            if (it != handlers_.end()) {
                it->second.callback(pkt.srcMac, pkt.data.data(),
                                    pkt.data.size(), it->second.context);
            }
            pending.pop();
        }

        // Fire deferred SEND_SUCCESS callbacks (drives the reliable transport's
        // ack, exactly as the esp32 driver does from its exec()).
        while (!sendResultQueue.empty()) {
            DeferredSendResult& r = sendResultQueue.front();
            auto it = sendStatusHandlers.find(r.type);
            if (it != sendStatusHandlers.end()) {
                it->second.callback(r.dst, r.data.data(), r.data.size(), true,
                                    it->second.context);
            }
            sendResultQueue.pop();
        }
    }

    /** Brings up the simulated peer-to-peer radio. */
    void enable() override { peerCommsState = PeerCommsState::ENABLED; }

    /** Tears down the simulated peer-to-peer radio. */
    void disable() override { peerCommsState = PeerCommsState::DISABLED; }

    /** Current simulated radio state. */
    PeerCommsState getPeerCommsState() override { return peerCommsState; }

    /** Transitions the simulated radio between enabled and disabled states. */
    void setPeerCommsState(PeerCommsState state) override {
        if (state == PeerCommsState::ENABLED && peerCommsState != PeerCommsState::ENABLED) {
            enable();
        } else if (state == PeerCommsState::DISABLED &&
                   peerCommsState != PeerCommsState::DISABLED) {
            disable();
        }
    }

    /** Sends a packet over the simulated peer broker when enabled. */
    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) override {
        if (peerCommsState != PeerCommsState::ENABLED) {
            return -1;
        }

        PacketHistoryEntry entry;
        entry.isSent = true;
        entry.srcMac = getMacString();
        entry.dstMac = macToString(dst);
        entry.packetType = packetType;
        entry.length = length;
        addToHistory(entry);
        
        NativePeerBroker::getInstance().sendPacket(macAddress_, dst, packetType, data, length);

        // Report SEND_SUCCESS on the next exec() (never inline: that would
        // re-enter Resender::send mid-transmit). The sim's broker delivery is
        // reliable, so every send succeeds.
        if (sendStatusHandlers.count(packetType)) {
            DeferredSendResult r;
            r.type = packetType;
            memcpy(r.dst, dst, 6);
            r.data.assign(data, data + length);
            sendResultQueue.push(std::move(r));
        }
        return 0;
    }

    void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) override {
        handlers_[packetType] = {callback, ctx};
    }

    void clearPacketHandler(PktType packetType) override {
        handlers_.erase(packetType);
    }

    /**
     * Registers the SEND_SUCCESS/SEND_FAIL observer for a PktType; the sim
     * reports success for every send on the next exec().
     */
    void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) override {
        sendStatusHandlers[packetType] = {callback, ctx};
    }

    /** Drops the send-status observer for a PktType. */
    void clearSendStatusHandler(PktType packetType) override {
        sendStatusHandlers.erase(packetType);
    }

    const uint8_t* getGlobalBroadcastAddress() override {
        return NativePeerBroker::getInstance().getBroadcastAddress();
    }

    uint8_t* getMacAddress() override {
        return macAddress_;
    }

    /**
     * Called by the broker to deliver a packet to this peer.
     * Enqueues the packet for processing on the next exec() call.
     */
    void receivePacket(const uint8_t* srcMac, PktType packetType, 
                       const uint8_t* data, size_t length) {
        if (peerCommsState != PeerCommsState::ENABLED) {
            return;
        }

        DeferredPacket pkt;
        pkt.type = packetType;
        memcpy(pkt.srcMac, srcMac, 6);
        pkt.data.assign(data, data + length);

        std::lock_guard<std::mutex> lock(recvMutex_);
        recvQueue_.push(std::move(pkt));
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

    /**
     * Get radio state as string for display.
     */
    std::string getStateString() const {
        return peerCommsState == PeerCommsState::ENABLED ? "ENABLED" : "DISABLED";
    }
    
    /**
     * Get packet history for CLI display.
     */
    const std::deque<PacketHistoryEntry>& getPacketHistory() const {
        return packetHistory_;
    }

private:
    struct HandlerEntry {
        PacketCallback callback;
        void* context;
    };

    struct DeferredPacket {
        PktType type;
        uint8_t srcMac[6];
        std::vector<uint8_t> data;
    };

    struct SendStatusEntry {
        PeerCommsInterface::SendStatusCallback callback;
        void* context;
    };

    // A send whose SEND_SUCCESS is reported on the next exec() (see send()/exec()).
    struct DeferredSendResult {
        PktType type;
        uint8_t dst[6];
        std::vector<uint8_t> data;
    };

    std::map<PktType, HandlerEntry> handlers_;
    std::map<PktType, SendStatusEntry> sendStatusHandlers;
    std::queue<DeferredSendResult> sendResultQueue;
    std::mutex recvMutex_;
    std::queue<DeferredPacket> recvQueue_;
    uint8_t macAddress_[6];
    PeerCommsState peerCommsState = PeerCommsState::DISABLED;
    std::deque<PacketHistoryEntry> packetHistory_;
    static const size_t MAX_HISTORY = 5;
    
    void addToHistory(const PacketHistoryEntry& entry) {
        packetHistory_.push_back(entry);
        while (packetHistory_.size() > MAX_HISTORY) {
            packetHistory_.pop_front();
        }
    }
    
    static std::string macToString(const uint8_t* mac) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return std::string(buf);
    }
};

// Implementation of broker's deliverPackets that depends on NativePeerCommsDriver
inline void NativePeerBroker::deliverPackets() {
    // Copy pending packets and peer map while holding lock, then release
    // before delivering. This prevents deadlock when a packet handler
    // tries to send a response packet (which would try to acquire mutex_).
    std::vector<PeerPacket> packetsToDeliver;
    std::map<std::array<uint8_t, 6>, NativePeerCommsDriver*> peersCopy;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!pendingPackets_.empty()) {
            packetsToDeliver.push_back(std::move(pendingPackets_.front()));
            pendingPackets_.pop();
        }
        peersCopy = peers_;
    }
    
    // Now deliver packets without holding the lock
    for (auto& packet : packetsToDeliver) {
        if (packet.isBroadcast) {
            // Deliver to all peers except sender
            for (auto& [mac, peer] : peersCopy) {
                if (!macEquals(mac, packet.srcMac.data())) {
                    peer->receivePacket(packet.srcMac.data(), packet.packetType,
                                       packet.data.data(), packet.data.size());
                }
            }
        } else {
            // Deliver to specific peer
            auto it = peersCopy.find(packet.dstMac);
            if (it != peersCopy.end()) {
                it->second->receivePacket(packet.srcMac.data(), packet.packetType,
                                         packet.data.data(), packet.data.size());
            }
        }
    }
}
