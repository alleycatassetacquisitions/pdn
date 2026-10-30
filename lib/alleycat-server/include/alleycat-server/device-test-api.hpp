#pragma once

#include "alleycat-server/device-test-codec.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

#include <functional>
#include <string>

namespace DeviceTestApi {

constexpr const char POST_DEVICE_REPORTS_ENDPOINT[] = "/device-reports";

/**
 * POST /device-reports — encodes @p result as WriteDeviceReportRequest protobuf and
 * queues the upload on @p wirelessManager. The server returns 204 with an empty body
 * when the report is saved or already stored; errors are plain text.
 *
 * @p onSuccess receives the empty response body on 204. Retry the same payload on
 * connection failure or 5xx; generate a new inspection UUID only for a new check.
 */
void uploadDeviceReport(WirelessManager* wirelessManager, const DeviceTestResult& result,
                        const std::function<void(const std::string&)>& onSuccess,
                        const std::function<void(const WirelessErrorInfo&)>& onError);

}  // namespace DeviceTestApi
