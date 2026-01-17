#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include "peer-comms-types.hpp"

class PeerCommsInterface {
public:
    using PeerAddress = std::array<uint8_t, 6>;
    using PacketCallback = std::function<void(const PeerAddress& src, const uint8_t* data, const size_t length, void* ctx)>;

    virtual ~PeerCommsInterface() {}
    virtual int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) = 0;
    virtual void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) = 0;
    virtual void clearPacketHandler(PktType packetType) = 0;

protected:

};