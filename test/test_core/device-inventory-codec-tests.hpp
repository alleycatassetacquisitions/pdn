#pragma once

#include <gtest/gtest.h>
#include <cstring>

#include "alleycat-server/device-inventory-codec.hpp"
#include "alleycat-server/device_api.pb.h"
#include "device/drivers/platform-clock.hpp"
#include "utils/uuid-bytes.hpp"
#include "utils/simple-timer.hpp"
#include <pb_decode.h>

namespace {

class FixedUnixClock : public PlatformClock {
public:
    /** Returns @p unixMs from @ref unixMilliseconds. */
    explicit FixedUnixClock(int64_t unixMs)
        : unixMs(unixMs) {}

    /** @copydoc PlatformClock::milliseconds */
    unsigned long milliseconds() override { return 0; }

    /** @copydoc PlatformClock::unixMilliseconds */
    int64_t unixMilliseconds() override { return unixMs; }

private:
    int64_t unixMs;
};

bool isNilUuid(const pb_byte_t* bytes) {
    for (size_t index = 0; index < 16; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

inline bool decodeDeviceReportPayload(const std::string& payload,
                                      alleycat_device_WriteDeviceReportRequest& out) {
    memset(&out, 0, sizeof(out));
    pb_istream_t stream =
    pb_istream_from_buffer(reinterpret_cast<const pb_byte_t*>(payload.data()), payload.size());
    return pb_decode(&stream, alleycat_device_WriteDeviceReportRequest_fields, &out);
}

inline void deviceInventoryCodecRoundTripEncodesFields() {
    FixedUnixClock clock(1792359000000LL);
    SimpleTimer::setPlatformClock(&clock);

    DeviceTestResult result;
    ASSERT_TRUE(UUIDBytes::parse("7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11", result.deviceId));
    ASSERT_TRUE(UUIDBytes::parse("8e2a1d63-4b7f-5a1c-0d2e-3f9b5c7d1e22", result.inspectionId));
    result.inspectedAtUnixMs = 1792359000000LL;
    result.hardwareVersion = 2;
    strncpy(result.firmwareCommitHash, "26a3bc12", sizeof(result.firmwareCommitHash));
    result.idLabel = 214;
    strncpy(result.macAddress, "aa:bb:cc:dd:ee:ff", sizeof(result.macAddress));
    result.components.screen = true;
    result.components.leds = true;
    result.components.primaryButton = true;
    result.components.secondaryButton = false;
    result.components.haptics = true;
    result.components.powerSwitch = true;
    result.components.inputJack = true;
    result.components.outputJack = true;
    result.components.housing = true;
    result.powersOn = true;

    std::string payload;
    ASSERT_TRUE(DeviceInventoryCodec::encodeRequest(result, payload));
    ASSERT_FALSE(payload.empty());

    alleycat_device_WriteDeviceReportRequest message;
    ASSERT_TRUE(decodeDeviceReportPayload(payload, message));
    ASSERT_TRUE(message.has_device);
    ASSERT_TRUE(message.has_inspection);
    EXPECT_EQ(message.device.id_label, 214u);
    EXPECT_EQ(message.device.hardware_version, 2u);
    EXPECT_EQ(message.device.device_kind, alleycat_device_DeviceKind_DEVICE_KIND_PDN);
    EXPECT_STREQ(message.device.mac_address, "aa:bb:cc:dd:ee:ff");
    EXPECT_EQ(message.inspection.which_checks,
              alleycat_device_CreateDeviceInspectionRequest_pdn_tag);
    EXPECT_TRUE(message.inspection.has_inspected_at_unix_ms);
    EXPECT_EQ(message.inspection.inspected_at_unix_ms, 1792359000000LL);
    EXPECT_TRUE(message.inspection.has_powers_on);
    EXPECT_TRUE(message.inspection.powers_on);
    EXPECT_STREQ(message.inspection.firmware_commit_hash, "26a3bc12");
    EXPECT_TRUE(message.inspection.checks.pdn.has_screen);
    EXPECT_TRUE(message.inspection.checks.pdn.screen);
    EXPECT_TRUE(message.inspection.checks.pdn.has_secondary_button);
    EXPECT_FALSE(message.inspection.checks.pdn.secondary_button);
}

inline void deviceInventoryCodecDerivesDeviceIdFromMacWhenUnset() {
    FixedUnixClock clock(1792359000000LL);
    SimpleTimer::setPlatformClock(&clock);

    DeviceTestResult result;
    ASSERT_TRUE(UUIDBytes::parse("8e2a1d63-4b7f-5a1c-0d2e-3f9b5c7d1e22", result.inspectionId));
    result.inspectedAtUnixMs = 1792359000000LL;
    result.idLabel = 42;
    strncpy(result.firmwareCommitHash, "deadbeef", sizeof(result.firmwareCommitHash));
    strncpy(result.macAddress, "aa:bb:cc:dd:ee:ff", sizeof(result.macAddress));
    result.powersOn = false;

    std::string payload;
    ASSERT_TRUE(DeviceInventoryCodec::encodeRequest(result, payload));

    alleycat_device_WriteDeviceReportRequest message;
    ASSERT_TRUE(decodeDeviceReportPayload(payload, message));
    EXPECT_FALSE(isNilUuid(message.device.id));
    EXPECT_EQ(message.device.id[0], 0xaa);
    EXPECT_EQ(message.device.id[5], 0xff);
    EXPECT_TRUE(message.inspection.has_powers_on);
    EXPECT_FALSE(message.inspection.powers_on);
}
