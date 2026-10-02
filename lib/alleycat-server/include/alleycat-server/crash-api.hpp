#pragma once

#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"
#include "device/crash/crash-record.hpp"
#include <functional>
#include <string>

namespace CrashApi {

/**
 * Queue a POST /device-logs request for the given crash record.
 *
 * Encodes @p record as a protobuf WriteDeviceLogRequest, sets Content-Type
 * to application/protobuf, and hands the request to @p wirelessManager.
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
    // TODO: encode record + deviceMac into a WriteDeviceLogRequest protobuf
    // payload using the nanopb-generated device_api.pb.h once codegen is wired.
    HttpRequest request(
        "/device-logs",
        "POST",
        "application/protobuf",
        "",  // payload populated by encoder
        onSuccess,
        onError
    );

    wirelessManager->queueHttpRequest(request);
}

} // namespace CrashApi
