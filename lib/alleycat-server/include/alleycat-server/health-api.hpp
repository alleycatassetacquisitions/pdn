#pragma once

#include <functional>
#include <string>
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

namespace HealthApi {

constexpr const char GET_HEALTH_CHECK_ENDPOINT[] = "/health_check";

/** GET /health_check — empty 200 body; @p Esp32S3HttpClient syncs wall clock from the Date header.
 */
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
