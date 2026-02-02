#pragma once

#include <cstdint>
#include <functional>
#include "wireless/peer-comms-types.hpp"

enum class PeerCommsState {
    CONNECTED,
    DISCONNECTED,
};

class PeerCommsInterface {
public:
    using PacketCallback = std::function<void(const uint8_t* src, const uint8_t* data, const size_t length, void* ctx)>;

    virtual ~PeerCommsInterface() {}
    virtual int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) = 0;
    virtual void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) = 0;
    virtual void clearPacketHandler(PktType packetType) = 0;
    virtual const uint8_t* getGlobalBroadcastAddress() = 0;
    virtual uint8_t* getMacAddress() = 0;
    virtual void removePeer(uint8_t* macAddr) = 0;
    virtual void setPeerCommsState(PeerCommsState state) = 0;
    virtual PeerCommsState getPeerCommsState() = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;

protected:

};