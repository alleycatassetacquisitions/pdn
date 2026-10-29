#pragma once

#include "alleycat-server/device-test-codec.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

#include <functional>
#include <string>

namespace DeviceTestApi {

constexpr const char POST_DEVICE_TEST_RESULTS_ENDPOINT[] = "/device-test-results";

inline void uploadResult(WirelessManager* wirelessManager,
                         const DeviceTestResult& result,
                         const std::function<void(const std::string&)>& onSuccess,
                         const std::function<void(const WirelessErrorInfo&)>& onError) {
    std::string payload;
    if (!DeviceTestCodec::encodeRequest(result, payload)) {
        onError({WirelessError::INVALID_STATE, "protobuf encode failed"});
        return;
    }

    HttpRequest request(POST_DEVICE_TEST_RESULTS_ENDPOINT,
                        "POST",
                        "application/protobuf",
                        std::move(payload),
                        onSuccess,
                        onError);
    wirelessManager->queueHttpRequest(std::move(request));
}

}  // namespace DeviceTestApi
