#pragma once

#include <cstring>
#include <functional>
#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

/**
 * The PDN end of player provisioning: sends the entered PIN to the cabled FDN
 * and hands each PlayerProvisionPacket it answers with to the current listener.
 * Both directions ride reliable channels, so a frame the radio drops is resent.
 */
class PdnProvisioningClient {
public:
    using OnReply = std::function<void(const uint8_t* fromMac, const PlayerProvisionPacket&)>;

    /** Claims kPlayerProvisionRequest (sent) and kPlayerProvision (received) on @p wirelessManager.
     */
    explicit PdnProvisioningClient(WirelessManager* wirelessManager)
        : resender(wirelessManager)
        , requestChannel(wirelessManager, &resender, PktType::kPlayerProvisionRequest, nullptr)
        , replyChannel(wirelessManager, &resender, PktType::kPlayerProvision, nullptr) {
        replyChannel.onReceive([this](const uint8_t* fromMac, const PlayerProvisionPacket& pkt) {
            if (onReply) onReply(fromMac, pkt);
        });
        resender.setAbandonCallback(
        [this](PktType type, uint8_t, const uint8_t*, const uint8_t*, size_t) {
            if (type == PktType::kPlayerProvisionRequest) requestAbandoned = true;
        });
    }

    /** Pumps request retries; call every tick while a request may be in flight. */
    void sync() { resender.sync(); }

    /** Asks the FDN at @p fdnMac to look up @p pin for this PDN. */
    void sendRequest(const uint8_t* fdnMac, const char* pin) {
        PlayerProvisionRequestPacket request{};
        strncpy(request.pin, pin, sizeof(request.pin) - 1);
        requestChannel.sendReliable(fdnMac, request);
    }

    /** @return true once, after the radio gave up on a request; the caller sends it again. */
    bool takeRequestAbandoned() {
        const bool abandoned = requestAbandoned;
        requestAbandoned = false;
        return abandoned;
    }

    /** Sets the reply listener; nullptr drops replies, so none land outside provisioning. */
    void setOnReply(OnReply listener) { onReply = std::move(listener); }

private:
    Resender resender;
    ReliableChannel<PlayerProvisionRequestPacket> requestChannel;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    OnReply onReply;
    bool requestAbandoned = false;
};
