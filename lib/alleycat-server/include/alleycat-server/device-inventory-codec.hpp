#pragma once

#include "alleycat-server/device-inventory-result.hpp"
#include "alleycat-server/device_api.pb.h"
#include "device/drivers/platform-clock.hpp"
#include "utils/simple-timer.hpp"
#include "utils/uuid-bytes.hpp"

#include <pb_encode.h>
#include <climits>
#include <cstring>
#include <string>

namespace DeviceInventoryCodec {

namespace detail {

inline bool appendToPayload(pb_ostream_t* stream, const pb_byte_t* buffer, size_t count) {
    std::string* payload = static_cast<std::string*>(stream->state);
    payload->append(reinterpret_cast<const char*>(buffer), count);
    return true;
}

inline void setOptionalBool(bool value, bool* hasField, bool* field) {
    *hasField = true;
    *field = value;
}

inline alleycat_device_PdnComponentChecks toPdnChecks(const DeviceTestComponents& components) {
    alleycat_device_PdnComponentChecks message{};
    setOptionalBool(components.screen, &message.has_screen, &message.screen);
    setOptionalBool(components.leds, &message.has_leds, &message.leds);
    setOptionalBool(components.primaryButton, &message.has_primary_button, &message.primary_button);
    setOptionalBool(components.secondaryButton, &message.has_secondary_button,
                    &message.secondary_button);
    setOptionalBool(components.haptics, &message.has_haptics, &message.haptics);
    setOptionalBool(components.powerSwitch, &message.has_power_switch, &message.power_switch);
    setOptionalBool(components.inputJack, &message.has_input_jack, &message.input_jack);
    setOptionalBool(components.outputJack, &message.has_output_jack, &message.output_jack);
    setOptionalBool(components.housing, &message.has_housing, &message.housing);
    return message;
}

inline bool toMessage(const DeviceTestResult& result,
                      alleycat_device_WriteDeviceReportRequest& message) {
    memset(&message, 0, sizeof(message));
    message.has_device = true;
    message.has_inspection = true;

    if (result.deviceId.isZero()) {
        return false;
    }
    memcpy(message.device.id, result.deviceId.bytes, UUIDBytes::SIZE);

    if (result.idLabel < 1 || result.idLabel > 9999) {
        return false;
    }
    message.device.id_label = static_cast<uint32_t>(result.idLabel);
    message.device.has_hardware_version = true;
    message.device.hardware_version = static_cast<uint32_t>(result.hardwareVersion);
    message.device.device_kind = alleycat_device_DeviceKind_DEVICE_KIND_PDN;

    if (result.macAddress[0] != '\0') {
        message.device.has_mac_address = true;
        strncpy(message.device.mac_address, result.macAddress,
                sizeof(message.device.mac_address) - 1);
    }

    if (result.inspectionId.isZero()) {
        return false;
    }
    memcpy(message.inspection.id, result.inspectionId.bytes, UUIDBytes::SIZE);

    int64_t inspectedAt = result.inspectedAtUnixMs;
    if (inspectedAt <= 0) {
        PlatformClock* clock = SimpleTimer::getPlatformClock();
        if (clock == nullptr) {
            return false;
        }
        inspectedAt = clock->unixMilliseconds();
    }
    if (inspectedAt <= 0) {
        return false;
    }
    message.inspection.has_inspected_at_unix_ms = true;
    message.inspection.inspected_at_unix_ms = inspectedAt;

    if (result.firmwareCommitHash[0] == '\0') {
        return false;
    }
    message.inspection.has_firmware_commit_hash = true;
    strncpy(message.inspection.firmware_commit_hash, result.firmwareCommitHash,
            sizeof(message.inspection.firmware_commit_hash) - 1);

    message.inspection.has_powers_on = true;
    message.inspection.powers_on = result.powersOn;

    message.inspection.which_checks = alleycat_device_CreateDeviceInspectionRequest_pdn_tag;
    message.inspection.checks.pdn = toPdnChecks(result.components);
    return true;
}

}  // namespace detail

/** Encodes @p result as WriteDeviceReportRequest into @p payload. */
inline bool encodeRequest(const DeviceTestResult& result, std::string& payload) {
    payload.clear();
    alleycat_device_WriteDeviceReportRequest message{};
    if (!detail::toMessage(result, message)) {
        return false;
    }

    pb_ostream_t stream = {};
    stream.callback = detail::appendToPayload;
    stream.state = &payload;
    stream.max_size = SIZE_MAX;

    return pb_encode(&stream, alleycat_device_WriteDeviceReportRequest_fields, &message);
}

}  // namespace DeviceInventoryCodec
