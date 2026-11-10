#pragma once

#include <cstring>
#include <functional>
#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "wireless/reliable-channel.hpp"

/**
 * PDN end of player provisioning: sends the PIN over a reliable channel and
 * forwards kPlayerProvision replies to the current listener.
 */
class PdnProvisioningClient {
public:
    using OnReply = std::function<void(const uint8_t* fromMac, const PlayerProvisionPacket&)>;

    /** Claims the PIN request (sent) and provision reply (received) packet types. */
    explicit PdnProvisioningClient(WirelessManager* wirelessManager)
        : requestChannel(wirelessManager, PktType::kPlayerProvisionRequest)
        , replyChannel(wirelessManager, PktType::kPlayerProvision) {
        requestChannel.setOnAbandon(
        [this](uint8_t, const uint8_t*, const PlayerProvisionRequestPacket&) {
            requestAbandoned = true;
        });
    }

    /** Asks the FDN at @p fdnMac to look up @p pin for this PDN. */
    void sendRequest(const uint8_t* fdnMac, const char* pin) {
        requestAbandoned = false;
        PlayerProvisionRequestPacket request{};
        strncpy(request.pin, pin, sizeof(request.pin) - 1);
        requestChannel.sendReliable(fdnMac, request);
    }

    /** @return true when the transport gave up on the last request; sendRequest clears it. */
    bool isRequestAbandoned() const { return requestAbandoned; }

    /** Sets the reply listener; nullptr drops replies, so none land outside provisioning. */
    void setOnReply(OnReply listener) { replyChannel.onReceive(std::move(listener)); }

private:
    ReliableChannel<PlayerProvisionRequestPacket> requestChannel;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    bool requestAbandoned = false;
};
