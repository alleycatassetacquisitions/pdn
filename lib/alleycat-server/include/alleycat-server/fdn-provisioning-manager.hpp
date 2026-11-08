#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "alleycat-server/player-api.hpp"
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
     * Serializes a populated Player's registration fields into a PlayerProvisionPacket,
     * stamped with @p serverUnixMs.
     */
    static PlayerProvisionPacket toProvisionPacket(const Player& player, int64_t serverUnixMs) {
        PlayerProvisionPacket pkt{};
        pkt.status = static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED);
        memcpy(pkt.playerId, player.getServerPlayerId().bytes, UUIDBytes::SIZE);
        strncpy(pkt.playerName, player.getServerPlayerName(), sizeof(pkt.playerName) - 1);
        pkt.playerName[sizeof(pkt.playerName) - 1] = '\0';
        const Team& team = player.getRegistrationTeam();
        memcpy(pkt.teamId, team.id.bytes, UUIDBytes::SIZE);
        strncpy(pkt.teamName, team.name, sizeof(pkt.teamName) - 1);
        pkt.teamName[sizeof(pkt.teamName) - 1] = '\0';

        const std::vector<Affiliation>& affiliations = player.getRegistrationAffiliations();
        const uint8_t count =
        static_cast<uint8_t>(std::min<size_t>(affiliations.size(), PlayerLimits::MAX_AFFILIATIONS));
        pkt.affiliationCount = count;
        for (uint8_t i = 0; i < count; ++i) {
            memcpy(pkt.affiliations[i].categoryId, affiliations[i].categoryId.bytes,
                   UUIDBytes::SIZE);
            strncpy(pkt.affiliations[i].categoryName, affiliations[i].categoryName,
                    sizeof(pkt.affiliations[i].categoryName) - 1);
            pkt.affiliations[i].categoryName[sizeof(pkt.affiliations[i].categoryName) - 1] = '\0';
            memcpy(pkt.affiliations[i].affiliationId, affiliations[i].affiliationId.bytes,
                   UUIDBytes::SIZE);
            strncpy(pkt.affiliations[i].affiliationName, affiliations[i].affiliationName,
                    sizeof(pkt.affiliations[i].affiliationName) - 1);
            pkt.affiliations[i].affiliationName[sizeof(pkt.affiliations[i].affiliationName) - 1] =
            '\0';
        }
        pkt.serverUnixMs = serverUnixMs;
        return pkt;
    }

    /** Decodes a PROFILE_DELIVERED PlayerProvisionPacket and applies it to @p outPlayer. */
    static bool applyProvisionPacket(const PlayerProvisionPacket& pkt, Player& outPlayer) {
        if (pkt.status != static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED)) {
            return false;
        }
        UUIDBytes playerId{};
        memcpy(playerId.bytes, pkt.playerId, UUIDBytes::SIZE);
        if (playerId.isZero()) {
            return false;
        }
        char playerName[PlayerLimits::NAME_BUFFER_SIZE] = {};
        strncpy(playerName, pkt.playerName, PlayerLimits::NAME_BUFFER_SIZE - 1);
        playerName[PlayerLimits::NAME_BUFFER_SIZE - 1] = '\0';
        if (playerName[0] == '\0') {
            return false;
        }

        Team team{};
        memcpy(team.id.bytes, pkt.teamId, UUIDBytes::SIZE);
        strncpy(team.name, pkt.teamName, PlayerLimits::NAME_BUFFER_SIZE - 1);
        team.name[PlayerLimits::NAME_BUFFER_SIZE - 1] = '\0';

        const size_t count = std::min<size_t>(pkt.affiliationCount, PlayerLimits::MAX_AFFILIATIONS);
        std::vector<Affiliation> affiliations;
        affiliations.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            Affiliation aff{};
            memcpy(aff.categoryId.bytes, pkt.affiliations[i].categoryId, UUIDBytes::SIZE);
            strncpy(aff.categoryName, pkt.affiliations[i].categoryName,
                    PlayerLimits::NAME_BUFFER_SIZE - 1);
            aff.categoryName[PlayerLimits::NAME_BUFFER_SIZE - 1] = '\0';
            memcpy(aff.affiliationId.bytes, pkt.affiliations[i].affiliationId, UUIDBytes::SIZE);
            strncpy(aff.affiliationName, pkt.affiliations[i].affiliationName,
                    PlayerLimits::NAME_BUFFER_SIZE - 1);
            aff.affiliationName[PlayerLimits::NAME_BUFFER_SIZE - 1] = '\0';
            affiliations.push_back(aff);
        }

        outPlayer.applyRegistrationProfile(playerId, playerName, team, affiliations);
        return true;
    }

    /** Looks up a player by PIN and delivers the provision result to the Connected PDN. */
    bool provisionPdn(const uint8_t* pdnMac, const std::string& pin) {
        if (!wirelessManager || !rdc || lookupInFlight ||
            !rdc->isConnectedPeer(DeviceType::PDN, pdnMac)) {
            return false;
        }

        std::array<uint8_t, 6> targetMac{};
        memcpy(targetMac.data(), pdnMac, 6);
        lookupInFlight = true;
        decodedPlayer = Player{};

        PlayerApi::getDevicePlayerByPin(
        wirelessManager, pin, &decodedPlayer,
        [this, targetMac](Player& player) {
            lookupInFlight = false;
            wirelessManager->enablePeerCommsMode();
            if (!rdc->isConnectedPeer(DeviceType::PDN, targetMac.data())) {
                return;
            }
            // The lookup's HTTP Date header has just synced this FDN's wall clock.
            PlatformClock* clock = SimpleTimer::getPlatformClock();
            PlayerProvisionPacket pkt =
            toProvisionPacket(player, clock ? clock->unixMilliseconds() : 0);
            wirelessManager->sendEspNowData(targetMac.data(), PktType::kPlayerProvision,
                                            reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        },
        [this, targetMac](const WirelessErrorInfo&) {
            lookupInFlight = false;
            wirelessManager->enablePeerCommsMode();
            if (!rdc->isConnectedPeer(DeviceType::PDN, targetMac.data())) {
                return;
            }
            PlayerProvisionPacket pkt{};
            pkt.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
            wirelessManager->sendEspNowData(targetMac.data(), PktType::kPlayerProvision,
                                            reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        });
        return true;
    }

private:
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
    Player decodedPlayer{};
};
