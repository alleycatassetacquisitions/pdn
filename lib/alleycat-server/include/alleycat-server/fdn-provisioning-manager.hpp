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
 * A lookup owns Wi-Fi from its HTTP request to its answer; while another
 * component owns it the lookup waits, inside its time limit.
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
     * Serves a parked request once its PDN is Connected, starts a lookup that
     * was waiting for Wi-Fi once it is free, and past
     * PROVISION_LOOKUP_TIMEOUT_MS cancels the lookup (see cancelLookup). Call
     * every tick.
     */
    void sync() {
        if (lookupInFlight && lookupTimer.expired()) {
            LOG_W("FDN_PROVISION", "Player lookup timed out");
            cancelLookup();
        }
        if (lookupInFlight && !lookupOwnsWifi) {
            requestLookup();
        }
        if (!parkedTimer.isRunning()) return;
        if (parkedTimer.expired()) {
            parkedTimer.invalidate();
        } else if (rdc->isConnectedPeer(DeviceType::PDN, parkedMac.data())) {
            parkedTimer.invalidate();
            provisionPdn(parkedMac.data(), parkedPin);
        }
    }

    /**
     * Ends the lookup in flight, if any, with LOOKUP_FAILED, without waiting
     * for the deadline. Wi-Fi is given up at once.
     */
    void cancelLookup() {
        if (!lookupInFlight) return;
        failLookup();
    }

    /** @return true from a PIN request being taken up until its lookup ends, the wait for Wi-Fi
     * included. */
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

        memcpy(lookupMac.data(), pdnMac, lookupMac.size());
        lookupPin = pin;
        lookupInFlight = true;
        // Covers the wait for Wi-Fi as well as the request.
        lookupTimer.setTimer(PROVISION_LOOKUP_TIMEOUT_MS);
        requestLookup();
    }

    // Queues the lookup's HTTP request if Wi-Fi can be had. While another
    // component owns it the lookup stays in flight, and sync() comes back here.
    void requestLookup() {
        if (!wirelessManager->acquireWifi(this)) return;
        lookupOwnsWifi = true;
        decodedPlayer = Player{};

        PlayerApi::getDevicePlayerByPin(
        wirelessManager, lookupPin, &decodedPlayer,
        [this](Player& player) {
            PlayerProvisionPacket pkt = PlayerProvisionCodec::encode(player);
            finishLookup(pkt, player.getServerPlayerName());
        },
        [this](const WirelessErrorInfo&) {
            // Giving Wi-Fi up fails the request of a lookup that has already ended.
            if (!lookupInFlight) return;
            failLookup();
        });
    }

    void failLookup() {
        PlayerProvisionPacket pkt{};
        pkt.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
        finishLookup(pkt, nullptr);
    }

    void finishLookup(PlayerProvisionPacket& pkt, const char* playerName) {
        lookupInFlight = false;
        lookupTimer.invalidate();
        // Before the reply: it can only be sent with the radio back on peer-to-peer.
        if (lookupOwnsWifi) {
            lookupOwnsWifi = false;
            wirelessManager->releaseWifi(this);
        }
        if (onLookupDone) onLookupDone(playerName);
        if (!rdc->isConnectedPeer(DeviceType::PDN, lookupMac.data())) {
            return;
        }
        strncpy(pkt.pin, lookupPin.c_str(), sizeof(pkt.pin) - 1);
        replyChannel.sendReliable(lookupMac.data(), pkt);
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
    bool lookupOwnsWifi = false;
    std::array<uint8_t, 6> lookupMac{};
    std::string lookupPin;
    bool lookupDuringPlayAllowed = false;
    SimpleTimer lookupTimer;
    SimpleTimer parkedTimer;
    std::array<uint8_t, 6> parkedMac{};
    std::string parkedPin;
    Player decodedPlayer{};
    OnLookupDone onLookupDone;
};
