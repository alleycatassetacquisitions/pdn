#pragma once

#include "alleycat-server/device_api.pb.h"
#include "utils/uuid-bytes.hpp"

#include <pb_encode.h>
#include <cstring>
#include <string>

namespace DevicePlayerResponseEncode {

inline bool appendBytesField(std::string& payload, uint32_t tag, const std::string& value) {
    std::string buffer(value.size() + 16, '\0');
    pb_ostream_t stream =
    pb_ostream_from_buffer(reinterpret_cast<pb_byte_t*>(buffer.data()), buffer.size());
    if (!pb_encode_tag(&stream, PB_WT_STRING, tag)) {
        return false;
    }
    if (!pb_encode_string(&stream, reinterpret_cast<const pb_byte_t*>(value.data()),
                          value.size())) {
        return false;
    }
    payload.append(buffer.data(), stream.bytes_written);
    return true;
}

/** Builds a minimal valid DevicePlayerResponse body for tests and CLI mocks. */
inline bool encodeMinimal(const UUIDBytes& playerId, const char* displayName,
                          std::string& outBody) {
    if (playerId.isZero() || displayName == nullptr) {
        return false;
    }
    outBody.clear();
    const std::string idWire(reinterpret_cast<const char*>(playerId.bytes), UUIDBytes::SIZE);
    if (!appendBytesField(outBody, alleycat_device_DevicePlayerResponse_id_tag, idWire)) {
        return false;
    }
    if (displayName[0] != '\0') {
        if (!appendBytesField(outBody, alleycat_device_DevicePlayerResponse_name_tag,
                              displayName)) {
            return false;
        }
    }
    return true;
}

}  // namespace DevicePlayerResponseEncode
