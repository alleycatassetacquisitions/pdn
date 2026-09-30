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

    /// Reports what the radio did with a previously queued frame. `data`/`length`
    /// are the payload as given to sendData (header stripped). A raw function
    /// pointer, not std::function: a registrant hands over a static member
    /// function or a captureless lambda and gets its instance back through
    /// ctx, so the driver's handler table holds no allocation.
    using SendStatusCallback = void (*)(const uint8_t* dstMac, const uint8_t* data, size_t length, bool success, void* ctx);

    virtual ~PeerCommsInterface() = default;
    virtual int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) = 0;
    virtual void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) = 0;
    virtual void clearPacketHandler(PktType packetType) = 0;

    /// Registers the handler invoked when the radio reports completion for a
    /// frame of this type. One handler per type; registering again replaces it.
    /// Default no-op so drivers that don't report send status keep compiling.
    virtual void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) {
        (void)packetType;
        (void)callback;
        (void)ctx;
    }

    /// Unregisters the send-status handler for a packet type, if any.
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