#include "alleycat-server/device-test-api.hpp"

#include "device/drivers/logger.hpp"

#define DEVICE_TEST_API_TAG "DeviceTestApi"

namespace DeviceTestApi {

void uploadDeviceReport(WirelessManager* wirelessManager, const DeviceTestResult& result,
                        const std::function<void(const std::string&)>& onSuccess,
                        const std::function<void(const WirelessErrorInfo&)>& onError) {
    std::string payload;
    if (!DeviceTestCodec::encodeRequest(result, payload)) {
        LOG_E(DEVICE_TEST_API_TAG,
              "Protobuf encode failed (idLabel=%d hw=%d mac=%s fw=%s inspectedAtMs=%lld)",
              result.idLabel, result.hardwareVersion, result.macAddress, result.firmwareCommitHash,
              static_cast<long long>(result.inspectedAtUnixMs));
        onError({WirelessError::INVALID_STATE, "protobuf encode failed"});
        return;
    }

    LOG_I(DEVICE_TEST_API_TAG, "Queuing POST %s (%zu bytes, idLabel=%d)",
          POST_DEVICE_REPORTS_ENDPOINT, payload.size(), result.idLabel);

    HttpRequest request(POST_DEVICE_REPORTS_ENDPOINT, "POST", "application/protobuf",
                        std::move(payload), onSuccess, onError);
    wirelessManager->queueHttpRequest(std::move(request));
}

}  // namespace DeviceTestApi
