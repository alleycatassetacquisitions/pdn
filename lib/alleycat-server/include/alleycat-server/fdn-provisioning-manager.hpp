#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include "alleycat-server/player-api.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "game/player.hpp"
#include "game/player-types.hpp"

/**
 * FDN-side player provisioning coordinator.
 *
 * Performs credential lookup (`GET /device/players/by-pin/{pin}`) over Wi-Fi for a
 * PDN that is Connected on an FDN serial jack, restores peer comms mode, and
 * delivers a PlayerProvisionPacket (PROFILE_DELIVERED or LOOKUP_FAILED) over
 * PktType::kPlayerProvision to the Connected PDN.
 */
class FdnProvisioningManager {
public:
    /** Constructs the FDN provisioning manager wired to wireless and RDC. */
    explicit FdnProvisioningManager(WirelessManager* wirelessManager,
                                    RemoteDeviceCoordinator* rdc = nullptr)
        : wirelessManager(wirelessManager)
        , rdc(rdc) {}

    /** Injects the RemoteDeviceCoordinator used to verify Connected PDN jacks. */
    void setRemoteDeviceCoordinator(RemoteDeviceCoordinator* coordinator) { rdc = coordinator; }

    /** Serializes a populated Player's registration fields into a PlayerProvisionPacket. */
    static PlayerProvisionPacket toProvisionPacket(const Player& player) {
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

        pkt.wins = static_cast<uint16_t>(std::max(0, player.getWins()));
        pkt.losses = static_cast<uint16_t>(std::max(0, player.getLosses()));
        pkt.matchesPlayed = static_cast<uint16_t>(std::max(0, player.getMatchesPlayed()));
        pkt.winStreak = static_cast<uint16_t>(std::max(0, player.getStreak()));
        pkt.lastReactionTimeMs = static_cast<uint32_t>(player.getLastReactionTime());
        pkt.totalReactionTimeMs = static_cast<uint32_t>(
        player.getAverageReactionTime() * static_cast<unsigned long>(pkt.matchesPlayed));
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

    /** Looks up a player by 4-digit PIN and delivers the provision result to the Connected PDN. */
    bool provisionConnectedPdnByPin(const std::string& pin) {
        if (!wirelessManager || lookupInFlight) {
            return false;
        }
        const uint8_t* pdnMac = findConnectedPdnMac();
        if (!pdnMac) {
            return false;
        }
        if (!PlayerApi::isValidPin(pin)) {
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
            if (!isConnectedPdnMac(targetMac.data())) {
                return;
            }
            PlayerProvisionPacket pkt = toProvisionPacket(player);
            wirelessManager->sendEspNowData(targetMac.data(), PktType::kPlayerProvision,
                                            reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        },
        [this, targetMac](const WirelessErrorInfo&) {
            lookupInFlight = false;
            wirelessManager->enablePeerCommsMode();
            if (!isConnectedPdnMac(targetMac.data())) {
                return;
            }
            PlayerProvisionPacket pkt{};
            pkt.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
            wirelessManager->sendEspNowData(targetMac.data(), PktType::kPlayerProvision,
                                            reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        });
        return true;
    }

    /** @return true while an HTTP PIN lookup is in flight. */
    bool isLookupInFlight() const { return lookupInFlight; }

private:
    const uint8_t* findConnectedPdnMac() const {
        if (!rdc) return nullptr;
        for (SerialIdentifier jack : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK,
                                      SerialIdentifier::INPUT_JACK_SECONDARY}) {
            if (rdc->getJackStatus(jack) == JackStatus::CONNECTED &&
                rdc->getPeerDeviceType(jack) == DeviceType::PDN) {
                const uint8_t* mac = rdc->getPeerMac(jack);
                if (mac) return mac;
            }
        }
        return nullptr;
    }

    bool isConnectedPdnMac(const uint8_t* mac) const {
        if (!rdc || !mac) return false;
        for (SerialIdentifier jack : {SerialIdentifier::INPUT_JACK, SerialIdentifier::OUTPUT_JACK,
                                      SerialIdentifier::INPUT_JACK_SECONDARY}) {
            if (rdc->getJackStatus(jack) == JackStatus::CONNECTED &&
                rdc->getPeerDeviceType(jack) == DeviceType::PDN) {
                const uint8_t* peer = rdc->getPeerMac(jack);
                if (peer && memcmp(peer, mac, 6) == 0) return true;
            }
        }
        return false;
    }

    WirelessManager* wirelessManager = nullptr;
    RemoteDeviceCoordinator* rdc = nullptr;
    bool lookupInFlight = false;
    Player decodedPlayer{};
};
