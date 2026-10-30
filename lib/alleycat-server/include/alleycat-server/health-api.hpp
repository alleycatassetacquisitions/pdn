#pragma once

#include <functional>
#include <string>
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

namespace HealthApi {

constexpr const char GET_HEALTH_CHECK_ENDPOINT[] = "/health_check";

/** GET /health_check; the HTTP Date header on any response sets the wall clock. */
inline void healthCheck(
    WirelessManager* wirelessManager,
    const std::function<void(const std::string&)>& onSuccess,
    const std::function<void(const WirelessErrorInfo&)>& onError) {
    HttpRequest request(
        GET_HEALTH_CHECK_ENDPOINT,
        "GET",
        "",
        "",
        onSuccess,
        onError);
    wirelessManager->queueHttpRequest(std::move(request));
}

}  // namespace HealthApi
