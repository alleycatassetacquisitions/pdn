#pragma once

#include <functional>
#include <string>
#include "alleycat-server/player-response.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

namespace PlayerApi {

/**
 * GET /api/players/{id} — parses JSON @ref PlayerResponse on success.
 */
inline void getPlayer(
    WirelessManager* wirelessManager,
    const std::string& playerId,
    const std::function<void(const PlayerResponse&)>& onSuccess,
    const std::function<void(const WirelessErrorInfo&)>& onError
) {
    const std::string path = "/api/players/" + playerId;

    HttpRequest request(
        path,
        "GET",
        "",
        "",
        [onSuccess, onError](const std::string& response) {
            PlayerResponse playerResponse;
            if (playerResponse.parseFromJson(response)) {
                onSuccess(playerResponse);
            } else {
                onError({
                    WirelessError::INVALID_RESPONSE,
                    "Failed to parse player response",
                    false
                });
            }
        },
        onError
    );

    wirelessManager->queueHttpRequest(request);
}

} // namespace PlayerApi
