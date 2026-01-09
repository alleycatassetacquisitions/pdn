#pragma once

#include <array>
#include <cstdint>
#include <functional>

class PeerCommsInterface {
public:
    using PeerAddress = std::array<uint8_t, 6>;
    using PacketCallback = std::function<void(const PeerAddress& src, const uint8_t* data, const size_t length, void* ctx)>;

    virtual ~PeerCommsInterface() {}
    virtual int initialize() = 0;
    virtual int sendData(const PeerAddress& dst, uint8_t packetType, const uint8_t* data, const size_t length) = 0;
    virtual void setPacketHandler(uint8_t packetType, PacketCallback callback, void* ctx) = 0;
    virtual void clearPacketHandler(uint8_t packetType) = 0;
    virtual void update() = 0;

protected:

};