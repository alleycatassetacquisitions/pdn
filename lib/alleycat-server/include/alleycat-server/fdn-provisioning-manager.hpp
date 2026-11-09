#pragma once

#include <array>
#include <cstring>
#include <functional>
#include <string>
#include "alleycat-server/player-api.hpp"
#include "alleycat-server/player-provision-codec.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "game/player.hpp"
#include "game/player-types.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

/**
 * FDN-side player provisioning coordinator.
 *
 * On a PlayerProvisionRequestPacket from a PDN Connected on an FDN jack, looks the
 * PIN up (`GET /device/players/by-pin/{pin}`) over Wi-Fi, restores peer comms mode,
 * and, if that PDN is still Connected, sends it a PlayerProvisionPacket
 * (PROFILE_DELIVERED or LOOKUP_FAILED). Request and reply ride reliable channels.
 */
class FdnProvisioningManager {
public:
    /** Reports a finished lookup: the delivered player's name, or nullptr on failure. */
    using OnLookupDone = std::function<void(const char* playerName)>;

    /** Constructs the FDN provisioning manager wired to wireless and RDC. */
    FdnProvisioningManager(WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
        : wirelessManager(wirelessManager)
        , rdc(rdc)
        , resender(wirelessManager)
        , requestChannel(wirelessManager, &resender, PktType::kPlayerProvisionRequest, nullptr)
        , replyChannel(wirelessManager, &resender, PktType::kPlayerProvision, nullptr) {
        requestChannel.onReceive(
        [this](const uint8_t* fromMac, const PlayerProvisionRequestPacket& request) {
            onProvisionRequest(fromMac, request);
        });
    }

    /**
     * Pumps reply retries and ends a lookup that has run past
     * PROVISION_LOOKUP_TIMEOUT_MS. Leaving Wi-Fi fails the queued request, which
     * clears the lookup and replies LOOKUP_FAILED; without it an unreachable AP
     * keeps the FDN deaf to ESP-NOW. Call every tick.
     */
    void sync() {
        resender.sync();
        if (lookupInFlight && lookupTimer.expired()) {
            LOG_W("FDN_PROVISION", "Player lookup timed out");
            lookupTimer.invalidate();
            wirelessManager->enablePeerCommsMode();
        }
    }

    /** @return true while a PIN lookup is waiting on the server. */
    bool isLookupInFlight() const { return lookupInFlight; }

    /** Sets the lookup-finished listener; nullptr clears it. */
    void setOnLookupDone(OnLookupDone listener) { onLookupDone = std::move(listener); }

    /** @return true when a PDN with no server player id is Connected on an FDN jack. */
    bool hasUnprovisionedPdnCabled() {
        if (!rdc) return false;
        for (SerialIdentifier jack :
             {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
            if (rdc->getJackStatus(jack) != JackStatus::CONNECTED ||
                rdc->getPeerDeviceType(jack) != DeviceType::PDN) {
                continue;
            }
            size_t length = 0;
            const uint8_t* bytes = rdc->getPeerProfile(jack, length);
            if (bytes == nullptr || length < sizeof(PlayerProfile)) continue;
            PlayerProfile profile{};
            memcpy(&profile, bytes, sizeof(profile));
            UUIDBytes playerId{};
            memcpy(playerId.bytes, profile.playerId, UUIDBytes::SIZE);
            if (playerId.isZero()) return true;
        }
        return false;
    }

private:
    // Shorter than the PDN's 10 s wait, so a request sent at mount is answered in time.
    static constexpr unsigned long PROVISION_LOOKUP_TIMEOUT_MS = 8000;

    void provisionPdn(const uint8_t* pdnMac, const std::string& pin) {
        if (!wirelessManager || !rdc || lookupInFlight ||
            !rdc->isConnectedPeer(DeviceType::PDN, pdnMac)) {
            return;
        }

        std::array<uint8_t, 6> targetMac{};
        memcpy(targetMac.data(), pdnMac, 6);
        lookupInFlight = true;
        lookupTimer.setTimer(PROVISION_LOOKUP_TIMEOUT_MS);
        decodedPlayer = Player{};

        PlayerApi::getDevicePlayerByPin(
        wirelessManager, pin, &decodedPlayer,
        [this, targetMac, pin](Player& player) {
            PlayerProvisionPacket pkt = PlayerProvisionCodec::encode(player);
            finishLookup(targetMac, pin, pkt, player.getServerPlayerName());
        },
        [this, targetMac, pin](const WirelessErrorInfo&) {
            PlayerProvisionPacket pkt{};
            pkt.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
            finishLookup(targetMac, pin, pkt, nullptr);
        });
    }

    void finishLookup(const std::array<uint8_t, 6>& targetMac, const std::string& pin,
                      PlayerProvisionPacket& pkt, const char* playerName) {
        lookupInFlight = false;
        lookupTimer.invalidate();
        wirelessManager->enablePeerCommsMode();
        if (onLookupDone) onLookupDone(playerName);
        if (!rdc->isConnectedPeer(DeviceType::PDN, targetMac.data())) {
            return;
        }
        strncpy(pkt.pin, pin.c_str(), sizeof(pkt.pin) - 1);
        replyChannel.sendReliable(targetMac.data(), pkt);
    }

    void onProvisionRequest(const uint8_t* fromMac, const PlayerProvisionRequestPacket& request) {
        // The PIN becomes a URL path segment, so only four ASCII digits leave the FDN.
        for (size_t i = 0; i < 4; ++i) {
            if (request.pin[i] < '0' || request.pin[i] > '9') {
                LOG_W("FDN_PROVISION", "Dropping provision request with a malformed PIN");
                return;
            }
        }
        provisionPdn(fromMac, std::string(request.pin, 4));
    }

    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* rdc = nullptr;
    Resender resender;
    ReliableChannel<PlayerProvisionRequestPacket> requestChannel;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    bool lookupInFlight = false;
    SimpleTimer lookupTimer;
    Player decodedPlayer{};
    OnLookupDone onLookupDone;
};
