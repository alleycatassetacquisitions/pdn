#pragma once

#include <functional>
#include <string>

#include "alleycat-server/device-player-codec.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "game/record-key.hpp"
#include "wireless/wireless-types.hpp"

namespace PlayerApi {

constexpr const char GET_DEVICE_PLAYERS_ENDPOINT[] = "/device/players/by-pin/";

/**
 * GET /device/players/by-pin/{pin} — decodes protobuf into @p player's registration fields
 * and hands the player's record key to @p recordKey. On success @p recordKey holds a key
 * only when the response carried one of RECORD_KEY_SIZE bytes and came over HTTPS with the
 * server's certificate checked. On a decode error it holds none; any other error leaves it
 * as it was.
 * The server answers 400 for a malformed PIN and 404 when no player matches.
 */
inline void getDevicePlayerByPin(WirelessManager* wirelessManager, const std::string& pin,
                                 Player* player, RecordKey* recordKey,
                                 const std::function<void(Player&)>& onSuccess,
                                 const std::function<void(const WirelessErrorInfo&)>& onError) {
    HttpRequest request(
    GET_DEVICE_PLAYERS_ENDPOINT + pin, "GET", "", "",
    [onSuccess, onError, player, recordKey, wirelessManager](const std::string& response) {
        if (player == nullptr || recordKey == nullptr ||
            !DevicePlayerCodec::decode(response, *player, *recordKey)) {
            onError({WirelessError::INVALID_RESPONSE, "Failed to decode device player response"});
            return;
        }
        // Anyone on the path of an unverified response could have read the key
        // or put their own in its place.
        if (recordKey->isHeld() && !wirelessManager->isHttpResponseServerVerified()) {
            LOG_E("PLAYER_API", "Record key refused: the lookup was not over verified HTTPS");
            recordKey->clear();
        }
        onSuccess(*player);
    },
    onError);

    request.requestHeaders.push_back({"Accept", "application/protobuf"});

    if (!wirelessManager->queueHttpRequest(std::move(request))) {
        onError({WirelessError::INVALID_STATE, "HTTP request not queued"});
    }
}

}  // namespace PlayerApi
