#pragma once

#include <functional>
#include <string>

#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/player-response.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "id-generator.hpp"
#include "wireless/wireless-types.hpp"

namespace PlayerApi {

constexpr const char GET_PLAYERS_ENDPOINT[] = "/api/players/";
constexpr const char GET_DEVICE_PLAYERS_ENDPOINT[] = "/device/players/by-pin/";

inline PlayerV2 toPlayerV2(const DevicePlayer& devicePlayer) {
    PlayerV2 player;
    player.id = devicePlayer.id;
    IdGenerator::copyId(player.name, devicePlayer.name);
    player.team.id = devicePlayer.teamId;
    IdGenerator::copyId(player.team.name, devicePlayer.teamName);
    player.affiliations.reserve(devicePlayer.affiliations.size());
    for (const DevicePlayerAffiliation& row : devicePlayer.affiliations) {
        Affiliation affiliation;
        affiliation.categoryId = row.categoryId;
        IdGenerator::copyId(affiliation.categoryName, row.categoryName);
        affiliation.affiliationId = row.affiliationId;
        IdGenerator::copyId(affiliation.affiliationName, row.affiliationName);
        player.affiliations.push_back(affiliation);
    }
    return player;
}

/**
 * GET /api/players/{id} — parses JSON @ref PlayerResponse on success.
 */
inline void getPlayer(
    WirelessManager* wirelessManager,
    const std::string& playerId,
    const std::function<void(const PlayerResponse&)>& onSuccess,
    const std::function<void(const WirelessErrorInfo&)>& onError) {
    HttpRequest request(
        GET_PLAYERS_ENDPOINT + playerId,
        "GET",
        "",
        "",
        [onSuccess, onError](const std::string& response) {
            PlayerResponse playerResponse;
            if (playerResponse.parseFromJson(response)) {
                onSuccess(playerResponse);
            } else {
                onError({WirelessError::INVALID_RESPONSE,
                         "Failed to parse player response"});
            }
        },
        onError);

    wirelessManager->queueHttpRequest(std::move(request));
}

/**
 * GET /device/players/by-pin/{pin} — decodes protobuf into @ref PlayerV2 on success.
 * The server answers 400 for a malformed PIN and 404 when no player matches.
 */
inline void getDevicePlayerByPin(WirelessManager* wirelessManager, const std::string& pin,
                                 const std::function<void(const PlayerV2&)>& onSuccess,
                                 const std::function<void(const WirelessErrorInfo&)>& onError) {
    HttpRequest request(
    GET_DEVICE_PLAYERS_ENDPOINT + pin, "GET", "", "",
    [onSuccess, onError](const std::string& response) {
        DevicePlayer devicePlayer;
        if (DevicePlayerDecoder::decode(response, devicePlayer)) {
            onSuccess(toPlayerV2(devicePlayer));
        } else {
            onError({WirelessError::INVALID_RESPONSE, "Failed to decode device player response"});
        }
    },
    onError);

    wirelessManager->queueHttpRequest(std::move(request));
}

}  // namespace PlayerApi
