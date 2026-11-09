#pragma once

#include <cstring>
#include <functional>
#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "wireless/reliable-channel.hpp"

/**
 * The PDN end of player provisioning: sends the entered PIN to the cabled FDN
 * and hands each PlayerProvisionPacket it answers with to the current listener.
 * Both directions ride reliable channels, so a frame the radio drops is resent.
 */
class PdnProvisioningClient {
public:
    using OnReply = std::function<void(const uint8_t* fromMac, const PlayerProvisionPacket&)>;

    /** Claims the PIN request (sent) and provision reply (received) packet types. */
    explicit PdnProvisioningClient(WirelessManager* wirelessManager)
        : requestChannel(wirelessManager, PktType::kPlayerProvisionRequest)
        , replyChannel(wirelessManager, PktType::kPlayerProvision) {
        replyChannel.onReceive([this](const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
            if (onReply) onReply(fromMac, pkt);
        });
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

    /** Drops the request's retries; nothing is owed or abandoned after it. */
    void cancelRequest() {
        requestChannel.cancelAll();
        requestAbandoned = false;
    }

    /** @return true when the radio gave up on the last request; sendRequest and cancelRequest clear
     * it. */
    bool isRequestAbandoned() const { return requestAbandoned; }

    /** Sets the reply listener; nullptr drops replies, so none land outside provisioning. */
    void setOnReply(OnReply listener) { onReply = std::move(listener); }

private:
    ReliableChannel<PlayerProvisionRequestPacket> requestChannel;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    OnReply onReply;
    bool requestAbandoned = false;
};
