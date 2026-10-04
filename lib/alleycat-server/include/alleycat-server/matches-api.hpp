#pragma once

#include <functional>
#include <string>
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

namespace MatchesApi {

/**
 * PUT /api/matches — @p matchesJson is the request body (application/json).
 */
inline void updateMatches(
    WirelessManager* wirelessManager,
    const std::string& matchesJson,
    const std::function<void(const std::string&)>& onSuccess,
    const std::function<void(const WirelessErrorInfo&)>& onError
) {
    HttpRequest request(
        "/api/matches",
        "PUT",
        "application/json",
        matchesJson,
        onSuccess,
        onError
    );
    wirelessManager->queueHttpRequest(request);
}

} // namespace MatchesApi
