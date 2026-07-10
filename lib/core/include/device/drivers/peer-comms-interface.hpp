#pragma once

#include <cstdint>
#include <functional>
#include "peer-comms-types.hpp"

enum class PeerCommsState {
    CONNECTED,
    DISCONNECTED,
};

class PeerCommsInterface {
public:
    using PacketCallback = std::function<void(const uint8_t* src, const uint8_t* data, const size_t length, void* ctx)>;
    // Fired once per outbound packet when the radio reports its MAC-layer
    // result. `dst`/`data`/`length` mirror the send; `success` is the
    // SEND_SUCCESS/FAIL verdict. Drives the reliable transport's ack in place
    // of a round-trip ack packet. Default no-op so drivers without a send
    // callback (or that don't need one) need not implement it.
    using SendStatusCallback = std::function<void(const uint8_t* dst, const uint8_t* data, const size_t length, bool success, void* ctx)>;

    virtual ~PeerCommsInterface() = default;
    virtual int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) = 0;
    virtual void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) = 0;
    virtual void clearPacketHandler(PktType packetType) = 0;
    virtual void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) { (void)packetType; (void)callback; (void)ctx; }
    virtual void clearSendStatusHandler(PktType packetType) { (void)packetType; }
    virtual const uint8_t* getGlobalBroadcastAddress() = 0;
    virtual uint8_t* getMacAddress() = 0;
    virtual void removePeer(uint8_t* macAddr) = 0;
    virtual int addEspNowPeer(const uint8_t* macAddr) = 0;
    virtual int removeEspNowPeer(const uint8_t* macAddr) = 0;
    virtual void setPeerCommsState(PeerCommsState state) = 0;
    virtual PeerCommsState getPeerCommsState() = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;

    // Returns the last observed RSSI for a peer, or -1 if unknown/unavailable.
    virtual int getRssiForPeer(const uint8_t* macAddr) { (void)macAddr; return -1; }

protected:

};