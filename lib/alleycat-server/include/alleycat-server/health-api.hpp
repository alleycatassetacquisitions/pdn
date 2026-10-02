#pragma once

#include <functional>
#include <string>
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

namespace HealthApi {

/** GET /health_check — JSON response body passed through to @p onSuccess. */
inline void healthCheck(
    WirelessManager* wirelessManager,
    const std::function<void(const std::string&)>& onSuccess,
    const std::function<void(const WirelessErrorInfo&)>& onError
) {
    HttpRequest request(
        "/health_check",
        "GET",
        "",
        "",
        onSuccess,
        onError
    );
    wirelessManager->queueHttpRequest(request);
}

} // namespace HealthApi
