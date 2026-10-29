#pragma once

#include "device-test/device-test-result.hpp"
#include <string>

namespace DeviceTestCodec {

/** Encodes @p result as WriteDeviceTestResultRequest into @p payload. */
bool encodeRequest(const DeviceTestResult& result, std::string& payload);

}  // namespace DeviceTestCodec
