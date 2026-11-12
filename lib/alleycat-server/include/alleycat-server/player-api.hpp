#pragma once

#include <functional>
#include <string>

#include "alleycat-server/device-player-codec.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "wireless/wireless-types.hpp"

namespace PlayerApi {

constexpr const char GET_DEVICE_PLAYERS_ENDPOINT[] = "/device/players/by-pin/";

/**
 * GET /device/players/by-pin/{pin} — decodes protobuf into @p player's registration fields.
 * The server answers 400 for a malformed PIN and 404 when no player matches.
 */
inline void getDevicePlayerByPin(WirelessManager* wirelessManager, const std::string& pin,
                                 Player* player, const std::function<void(Player&)>& onSuccess,
                                 const std::function<void(const WirelessErrorInfo&)>& onError) {
    HttpRequest request(
    GET_DEVICE_PLAYERS_ENDPOINT + pin, "GET", "", "",
    [onSuccess, onError, player](const std::string& response) {
        if (player != nullptr && DevicePlayerCodec::decode(response, *player)) {
            onSuccess(*player);
        } else {
            onError({WirelessError::INVALID_RESPONSE, "Failed to decode device player response"});
        }
    },
    onError);

    request.requestHeaders.push_back({"Accept", "application/protobuf"});

    if (!wirelessManager->queueHttpRequest(std::move(request))) {
        onError({WirelessError::INVALID_STATE, "HTTP request not queued"});
    }
}

}  // namespace PlayerApi
