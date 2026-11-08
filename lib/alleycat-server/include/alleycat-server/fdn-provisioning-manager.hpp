#pragma once

#include <array>
#include <cstring>
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

/**
 * FDN-side player provisioning coordinator.
 *
 * On a PlayerProvisionRequestPacket from a PDN Connected on an FDN jack, looks the
 * PIN up (`GET /device/players/by-pin/{pin}`) over Wi-Fi, restores peer comms mode,
 * and, if that PDN is still Connected, sends it a PlayerProvisionPacket
 * (PROFILE_DELIVERED or LOOKUP_FAILED) over PktType::kPlayerProvision.
 */
class FdnProvisioningManager {
public:
    /** Constructs the FDN provisioning manager wired to wireless and RDC. */
    FdnProvisioningManager(WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc)
        : wirelessManager(wirelessManager)
        , rdc(rdc) {
        if (this->wirelessManager) {
            this->wirelessManager->setEspNowPacketHandler(
            PktType::kPlayerProvisionRequest,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                static_cast<FdnProvisioningManager*>(ctx)->onProvisionRequest(fromMac, data,
                                                                              dataLen);
            },
            this);
        }
    }

    /** Clears the kPlayerProvisionRequest packet handler on destruction. */
    ~FdnProvisioningManager() {
        if (wirelessManager) {
            wirelessManager->clearEspNowPacketHandler(PktType::kPlayerProvisionRequest);
        }
    }

    /**
     * Ends a lookup that has run past PROVISION_LOOKUP_TIMEOUT_MS. Leaving Wi-Fi fails
     * the queued request, which clears the lookup and replies LOOKUP_FAILED; without
     * it an unreachable AP keeps the FDN deaf to ESP-NOW. Call every tick.
     */
    void sync() {
        if (lookupInFlight && lookupTimer.expired()) {
            LOG_W("FDN_PROVISION", "Player lookup timed out");
            lookupTimer.invalidate();
            wirelessManager->enablePeerCommsMode();
        }
    }

private:
    // Shorter than the PDN's 10 s wait, so a request sent at mount is answered in time.
    static constexpr unsigned long PROVISION_LOOKUP_TIMEOUT_MS = 8000;

    bool provisionPdn(const uint8_t* pdnMac, const std::string& pin) {
        if (!wirelessManager || !rdc || lookupInFlight ||
            !rdc->isConnectedPeer(DeviceType::PDN, pdnMac)) {
            return false;
        }

        std::array<uint8_t, 6> targetMac{};
        memcpy(targetMac.data(), pdnMac, 6);
        lookupInFlight = true;
        lookupTimer.setTimer(PROVISION_LOOKUP_TIMEOUT_MS);
        decodedPlayer = Player{};

        PlayerApi::getDevicePlayerByPin(
        wirelessManager, pin, &decodedPlayer,
        [this, targetMac, pin](Player& player) {
            lookupInFlight = false;
            lookupTimer.invalidate();
            wirelessManager->enablePeerCommsMode();
            if (!rdc->isConnectedPeer(DeviceType::PDN, targetMac.data())) {
                return;
            }
            // The lookup's HTTP Date header normally syncs this FDN's wall clock first.
            PlatformClock* clock = SimpleTimer::getPlatformClock();
            PlayerProvisionPacket pkt =
            PlayerProvisionCodec::encode(player, clock ? clock->unixMilliseconds() : 0);
            strncpy(pkt.pin, pin.c_str(), sizeof(pkt.pin) - 1);
            wirelessManager->sendEspNowData(targetMac.data(), PktType::kPlayerProvision,
                                            reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        },
        [this, targetMac, pin](const WirelessErrorInfo&) {
            lookupInFlight = false;
            lookupTimer.invalidate();
            wirelessManager->enablePeerCommsMode();
            if (!rdc->isConnectedPeer(DeviceType::PDN, targetMac.data())) {
                return;
            }
            PlayerProvisionPacket pkt{};
            pkt.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
            strncpy(pkt.pin, pin.c_str(), sizeof(pkt.pin) - 1);
            wirelessManager->sendEspNowData(targetMac.data(), PktType::kPlayerProvision,
                                            reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        });
        return true;
    }

    void onProvisionRequest(const uint8_t* fromMac, const uint8_t* data, size_t dataLen) {
        if (!data || dataLen != sizeof(PlayerProvisionRequestPacket)) {
            LOG_W("FDN_PROVISION", "Dropping provision request of %zu bytes", dataLen);
            return;
        }
        PlayerProvisionRequestPacket request{};
        memcpy(&request, data, sizeof(request));
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
    bool lookupInFlight = false;
    SimpleTimer lookupTimer;
    Player decodedPlayer{};
};
