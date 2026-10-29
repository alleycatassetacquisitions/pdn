#include "alleycat-server/device-test-codec.hpp"

#include "alleycat-server/device_api.pb.h"

#include <pb_encode.h>
#include <cstring>

namespace {

bool appendToPayload(pb_ostream_t* stream, const pb_byte_t* buffer, size_t count) {
    std::string* payload = static_cast<std::string*>(stream->state);
    payload->append(reinterpret_cast<const char*>(buffer), count);
    return true;
}

alleycat_device_DeviceTestComponentResults toComponentsMessage(const DeviceTestComponents& components) {
    alleycat_device_DeviceTestComponentResults message =
        alleycat_device_DeviceTestComponentResults_init_zero;
    message.has_screen = true;
    message.screen = components.screen;
    message.has_leds = true;
    message.leds = components.leds;
    message.has_primary_button = true;
    message.primary_button = components.primaryButton;
    message.has_secondary_button = true;
    message.secondary_button = components.secondaryButton;
    message.has_haptics = true;
    message.haptics = components.haptics;
    message.has_power_switch = true;
    message.power_switch = components.powerSwitch;
    message.has_input_jack = true;
    message.input_jack = components.inputJack;
    message.has_output_jack = true;
    message.output_jack = components.outputJack;
    message.has_housing = true;
    message.housing = components.housing;
    return message;
}

alleycat_device_WriteDeviceTestResultRequest toMessage(const DeviceTestResult& result) {
    alleycat_device_WriteDeviceTestResultRequest message =
        alleycat_device_WriteDeviceTestResultRequest_init_zero;
    message.has_hardware_version = true;
    message.hardware_version = static_cast<uint32_t>(result.hardwareVersion);
    message.has_id_label = true;
    message.id_label = static_cast<uint32_t>(result.idLabel);

    message.has_firmware_commit_hash = true;
    strncpy(message.firmware_commit_hash, result.firmwareCommitHash,
            sizeof(message.firmware_commit_hash) - 1);
    message.has_device_mac = true;
    strncpy(message.device_mac, result.macAddress, sizeof(message.device_mac) - 1);

    message.has_components = true;
    message.components = toComponentsMessage(result.components);
    return message;
}

}  // namespace

namespace DeviceTestCodec {

bool encodeRequest(const DeviceTestResult& result, std::string& payload) {
    payload.clear();
    const alleycat_device_WriteDeviceTestResultRequest message = toMessage(result);

    pb_ostream_t stream = pb_ostream_from_buffer(nullptr, 0);
    stream.callback = appendToPayload;
    stream.state = &payload;

    return pb_encode(&stream, alleycat_device_WriteDeviceTestResultRequest_fields, &message);
}

}  // namespace DeviceTestCodec
