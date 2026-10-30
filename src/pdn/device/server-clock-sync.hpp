#pragma once

#include "alleycat-server/health-api.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/platform-clock.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"

/** Queues GET /health_check once so the HTTP Date header can set the POSIX wall clock. */
inline void queueBootServerClockSync(WirelessManager* wirelessManager) {
    if (wirelessManager == nullptr) {
        return;
    }

    LOG_I("ServerClockSync", "Queuing GET %s (wall clock sync via HTTP Date header)",
          HealthApi::GET_HEALTH_CHECK_ENDPOINT);

    HealthApi::healthCheck(
    wirelessManager,
    [](const std::string& body) {
        PlatformClock* clock = SimpleTimer::getPlatformClock();
        LOG_I("ServerClockSync", "Health check OK (body %zu bytes); unixMs=%lld", body.size(),
              clock != nullptr ? static_cast<long long>(clock->unixMilliseconds()) : 0LL);
    },
    [](const WirelessErrorInfo& error) {
        LOG_W("ServerClockSync",
              "Health check failed: %s (code=%d); inspectedAt may be wrong until sync",
              error.message.c_str(), static_cast<int>(error.code));
    });
}
