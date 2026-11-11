#pragma once

#include <cstring>
#include <functional>
#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

/**
 * PDN end of player provisioning: sends the PIN over a reliable channel and
 * forwards kPlayerProvision replies to the current listener.
 */
class PdnProvisioningClient {
public:
    using OnReply = std::function<void(const uint8_t* fromMac, const PlayerProvisionPacket&)>;

    /** Claims the PIN request (sent) and provision reply (received) packet types. */
    explicit PdnProvisioningClient(WirelessManager* wirelessManager)
        : resender(wirelessManager)
        , requestChannel(wirelessManager, &resender, PktType::kPlayerProvisionRequest, nullptr)
        , replyChannel(wirelessManager, &resender, PktType::kPlayerProvision, nullptr) {
        resender.setAbandonCallback(
        [this](PktType type, uint8_t, const uint8_t*, const uint8_t*, size_t) {
            if (type == PktType::kPlayerProvisionRequest) requestAbandoned = true;
        });
    }

    /** Pumps request retries; call every tick while a request may be in flight. */
    void sync() { resender.sync(); }

    /** Asks the FDN at @p fdnMac to look up @p pin for this PDN. */
    void sendRequest(const uint8_t* fdnMac, const char* pin) {
        requestAbandoned = false;
        PlayerProvisionRequestPacket request{};
        strncpy(request.pin, pin, sizeof(request.pin) - 1);
        requestChannel.sendReliable(fdnMac, request);
    }

    /** @return true when the Resender gave up on the last request; sendRequest clears it. */
    bool isRequestAbandoned() const { return requestAbandoned; }

    /** Sets the reply listener; nullptr drops replies, so none land outside provisioning. */
    void setOnReply(OnReply listener) { replyChannel.onReceive(std::move(listener)); }

private:
    Resender resender;
    ReliableChannel<PlayerProvisionRequestPacket> requestChannel;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    bool requestAbandoned = false;
};
