#pragma once

#include "alleycat-server/crash-encoder.hpp"
#include "device/wireless-manager.hpp"
#include "device/crash/crash-record.hpp"
#include "wireless/wireless-types.hpp"
#include <cstring>
#include <functional>
#include <string>

namespace CrashApi {

/**
 * Encodes @p record as a WriteDeviceLogRequest protobuf and queues a
 * POST /device-logs request via @p wirelessManager.
 *
 * @param wirelessManager  Active wireless manager (auto-switches to WiFi mode).
 * @param record           Crash record to upload.
 * @param deviceMac        6-byte MAC address from HttpClientInterface::getMacAddress().
 * @param onSuccess        Called with the (empty) 204 response body on success.
 * @param onError          Called with error details on failure.
 */
inline void uploadCrash(
    WirelessManager* wirelessManager,
    const CrashRecord& record,
    const uint8_t* deviceMac,
    const std::function<void(const std::string&)>& onSuccess,
    const std::function<void(const WirelessErrorInfo&)>& onError
) {
    uint8_t buf[CrashEncoder::MAX_ENCODED_SIZE];
    const size_t len = CrashEncoder::encode(record, deviceMac, buf, sizeof(buf));

    if (len == 0) {
        onError({WirelessError::INVALID_STATE, "protobuf encode failed", false});
        return;
    }

    HttpRequest request(
        "/device-logs",
        "POST",
        "application/protobuf",
        std::string(reinterpret_cast<const char*>(buf), len),
        onSuccess,
        onError
    );

    wirelessManager->queueHttpRequest(request);
}

} // namespace CrashApi
