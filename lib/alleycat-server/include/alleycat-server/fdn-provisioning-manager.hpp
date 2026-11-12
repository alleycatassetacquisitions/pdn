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

/**
 * FDN side of player provisioning: answers a cabled PDN's PIN request with the
 * server's profile or LOOKUP_FAILED. Request and reply ride reliable channels.
 */
class FdnProvisioningManager {
public:
    /** Reports a finished lookup: the looked-up player's name, or nullptr on failure. */
    using OnLookupDone = std::function<void(const char* playerName)>;

    /** Constructs the FDN provisioning manager wired to wireless and RDC. */
    FdnProvisioningManager(WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
        : wirelessManager(wirelessManager)
        , rdc(rdc)
        , requestChannel(wirelessManager, PktType::kPlayerProvisionRequest)
        , replyChannel(wirelessManager, PktType::kPlayerProvision) {
        requestChannel.onReceive(
        [this](const uint8_t* fromMac, const PlayerProvisionRequestPacket& request) {
            onProvisionRequest(fromMac, request);
        });
    }

    /**
     * Serves a parked request once its PDN is Connected, and past
     * PROVISION_LOOKUP_TIMEOUT_MS leaves Wi-Fi, which makes the HTTP driver fail
     * the queued request and so ends the lookup with LOOKUP_FAILED. Call every tick.
     */
    void sync() {
        if (lookupInFlight && lookupTimer.expired()) {
            LOG_W("FDN_PROVISION", "Player lookup timed out");
            cancelLookup();
        }
        if (!parkedTimer.isRunning()) return;
        if (parkedTimer.expired()) {
            parkedTimer.invalidate();
        } else if (rdc->isConnectedPeer(DeviceType::PDN, parkedMac.data())) {
            parkedTimer.invalidate();
            provisionPdn(parkedMac.data(), parkedPin);
        }
    }

    /** Ends the lookup in flight, if any, by leaving Wi-Fi without waiting for the deadline. */
    void cancelLookup() {
        if (!lookupInFlight) return;
        lookupTimer.invalidate();
        wirelessManager->enablePeerCommsMode();
    }

    /** @return true while a PIN lookup is waiting on the server. */
    bool isLookupInFlight() const { return lookupInFlight; }

    /** Sets the lookup-finished listener; nullptr clears it. */
    void setOnLookupDone(OnLookupDone listener) { onLookupDone = std::move(listener); }

    /**
     * While not allowed, a request is dropped if a registered PDN is cabled, so a
     * lookup's Wi-Fi excursion never cuts into that PDN's symbol-match game.
     */
    void setLookupDuringPlayAllowed(bool allowed) { lookupDuringPlayAllowed = allowed; }

    /** @return true when a PDN with no applied profile is Connected on an FDN jack. */
    bool hasUnprovisionedPdnCabled() { return hasCabledPdn(false); }

    /** @return true when a PDN with an applied profile, demo included, is Connected. */
    bool hasRegisteredPdnCabled() { return hasCabledPdn(true); }

private:
    // The PDN can read its jack Connected, and send, a tick before this side's
    // jack commits. The radio acks the request, so the PDN never sends it again.
    static constexpr unsigned long PARK_REQUEST_MS = 1000;

    bool hasCabledPdn(bool registered) {
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
            // A demo player has no server id either, but is done registering.
            if ((profile.registered != 0) == registered) return true;
        }
        return false;
    }

    void provisionPdn(const uint8_t* pdnMac, const std::string& pin) {
        if (lookupInFlight) return;

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
        if (!lookupDuringPlayAllowed && hasRegisteredPdnCabled()) {
            LOG_W("FDN_PROVISION", "Dropping provision request during a registered PDN's game");
            return;
        }
        // The PIN becomes a URL path segment, so only four ASCII digits leave the FDN.
        for (size_t i = 0; i < 4; ++i) {
            if (request.pin[i] < '0' || request.pin[i] > '9') {
                LOG_W("FDN_PROVISION", "Dropping provision request with a malformed PIN");
                return;
            }
        }
        const std::string pin(request.pin, 4);
        if (!rdc->isConnectedPeer(DeviceType::PDN, fromMac)) {
            memcpy(parkedMac.data(), fromMac, parkedMac.size());
            parkedPin = pin;
            parkedTimer.setTimer(PARK_REQUEST_MS);
            return;
        }
        provisionPdn(fromMac, pin);
    }

    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* rdc = nullptr;
    ReliableChannel<PlayerProvisionRequestPacket> requestChannel;
    ReliableChannel<PlayerProvisionPacket> replyChannel;
    bool lookupInFlight = false;
    bool lookupDuringPlayAllowed = false;
    SimpleTimer lookupTimer;
    SimpleTimer parkedTimer;
    std::array<uint8_t, 6> parkedMac{};
    std::string parkedPin;
    Player decodedPlayer{};
    OnLookupDone onLookupDone;
};
