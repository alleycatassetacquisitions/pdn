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
    // Fired once per outbound packet the driver accepted, either when the radio
    // reports its MAC-layer result or when the radio refused the frame outright
    // and will report nothing. `dst`/`data`/`length` mirror the send; `success` is
    // the verdict. Drives the reliable transport's ack in place of a round-trip
    // ack packet. A driver that accepts a frame owes this callback: it is the only
    // delivery signal the reliable layer gets, and a frame never reported is one it
    // waits on until its own timer gives up. Defaulted to a no-op only so a driver
    // with no send callback at all still satisfies the interface -- a driver that
    // takes frames and leaves this unset starves the layer above it.
    using SendStatusCallback = std::function<void(const uint8_t* dst, const uint8_t* data, const size_t length, bool success, void* ctx)>;

    virtual ~PeerCommsInterface() = default;
    virtual int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) = 0;
    virtual void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) = 0;
    virtual void clearPacketHandler(PktType packetType) = 0;
    virtual void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) { (void)packetType; (void)callback; (void)ctx; }
    virtual void clearSendStatusHandler(PktType packetType) { (void)packetType; }
    virtual const uint8_t* getGlobalBroadcastAddress() = 0;
    virtual uint8_t* getMacAddress() = 0;
    virtual void setPeerCommsState(PeerCommsState state) = 0;
    virtual PeerCommsState getPeerCommsState() = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;

    // Returns the last observed RSSI for a peer, or -1 if unknown/unavailable.
    virtual int getRssiForPeer(const uint8_t* macAddr) { (void)macAddr; return -1; }

protected:

};