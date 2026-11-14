#pragma once

#ifdef NATIVE_BUILD

#include "alleycat-server/device_api.pb.h"
#include "game/record-key.hpp"
#include "utils/uuid-bytes.hpp"

#include <pb_encode.h>
#include <cstring>
#include <string>

namespace cli {

namespace DevicePlayerResponseEncode {

inline bool encodeMinimal(const UUIDBytes& playerId, const char* displayName, std::string& outBody) {
    if (playerId.isZero() || displayName == nullptr) {
        return false;
    }
    outBody.clear();
    const std::string idWire(reinterpret_cast<const char*>(playerId.bytes), UUIDBytes::SIZE);
    std::string buffer(idWire.size() + 16, '\0');
    pb_ostream_t stream =
        pb_ostream_from_buffer(reinterpret_cast<pb_byte_t*>(buffer.data()), buffer.size());
    if (!pb_encode_tag(&stream, PB_WT_STRING, alleycat_device_DevicePlayerResponse_id_tag) ||
        !pb_encode_string(&stream, reinterpret_cast<const pb_byte_t*>(idWire.data()), idWire.size())) {
        return false;
    }
    outBody.append(buffer.data(), stream.bytes_written);
    // SERVER-SEAM(pdn-record-auth-and-relay): record_key is a provisional field.
    // The simulated server gives every player the same key.
    const std::string keyWire(RECORD_KEY_SIZE, '\xA5');
    buffer.assign(keyWire.size() + 16, '\0');
    stream = pb_ostream_from_buffer(reinterpret_cast<pb_byte_t*>(buffer.data()), buffer.size());
    if (!pb_encode_tag(&stream, PB_WT_STRING,
                       alleycat_device_DevicePlayerResponse_record_key_tag) ||
        !pb_encode_string(&stream, reinterpret_cast<const pb_byte_t*>(keyWire.data()),
                          keyWire.size())) {
        return false;
    }
    outBody.append(buffer.data(), stream.bytes_written);
    if (displayName[0] != '\0') {
        const size_t nameLen = std::strlen(displayName);
        buffer.assign(nameLen + 16, '\0');
        stream = pb_ostream_from_buffer(reinterpret_cast<pb_byte_t*>(buffer.data()), buffer.size());
        if (!pb_encode_tag(&stream, PB_WT_STRING, alleycat_device_DevicePlayerResponse_name_tag) ||
            !pb_encode_string(
                &stream, reinterpret_cast<const pb_byte_t*>(displayName), nameLen)) {
            return false;
        }
        outBody.append(buffer.data(), stream.bytes_written);
    }
    return true;
}

}  // namespace DevicePlayerResponseEncode

}  // namespace cli

#endif  // NATIVE_BUILD
