#pragma once

#include "alleycat-server/device-inventory-codec.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

#include <functional>
#include <string>

namespace DeviceInventoryApi {

constexpr const char POST_DEVICE_REPORTS_ENDPOINT[] = "/device-reports";

/** POST /device-reports — encodes @p result as protobuf and queues on @p wirelessManager.
 * Reuse @p result.inspectionId across retries; generate a fresh one for each new inspection. */
inline void uploadDeviceReport(WirelessManager* wirelessManager, const DeviceTestResult& result,
                               const std::function<void(const std::string&)>& onSuccess,
                               const std::function<void(const WirelessErrorInfo&)>& onError) {
    std::string payload;
    if (!DeviceInventoryCodec::encodeRequest(result, payload)) {
        LOG_E("DeviceInventoryApi",
              "Protobuf encode failed (idLabel=%d hw=%d mac=%s fw=%s inspectedAtMs=%lld)",
              result.idLabel, result.hardwareVersion, result.macAddress, result.firmwareCommitHash,
              static_cast<long long>(result.inspectedAtUnixMs));
        onError({WirelessError::INVALID_STATE, "protobuf encode failed"});
        return;
    }

    LOG_I("DeviceInventoryApi", "Queuing POST %s (%zu bytes, idLabel=%d)",
          POST_DEVICE_REPORTS_ENDPOINT, payload.size(), result.idLabel);

    HttpRequest request(POST_DEVICE_REPORTS_ENDPOINT, "POST", "application/protobuf",
                        std::move(payload), onSuccess, onError);
    wirelessManager->queueHttpRequest(std::move(request));
}

}  // namespace DeviceInventoryApi
