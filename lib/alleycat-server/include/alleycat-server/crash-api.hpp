#pragma once

#include "alleycat-server/crash-codec.hpp"
#include "alleycat-server/fdn-stored-entry.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"
#include <cstring>
#include <functional>
#include <string>

namespace CrashApi {

constexpr const char POST_DEVICE_LOGS_ENDPOINT[] = "/device-logs";

/**
 * Encodes the crash record in @p entry as a WriteDeviceLogRequest protobuf and
 * queues a POST /device-logs request via @p wirelessManager. The upload names
 * the device the record is from, which for a relayed record is the PDN and not
 * this FDN. Encode and queue failures reach @p onError before this returns.
 *
 * @param wirelessManager  Active wireless manager (auto-switches to WiFi mode).
 * @param entry            The crash record and the 6-byte MAC address of the
 *                         device the record is from.
 * @param onSuccess        Called with the (empty) 204 response body on success.
 * @param onError          Called with error details on failure; httpStatus is
 *                         the server's answer when it gave one.
 */
inline void uploadCrash(WirelessManager* wirelessManager, const FdnStoredCrashEntry& entry,
                        const std::function<void(const std::string&)>& onSuccess,
                        const std::function<void(const WirelessErrorInfo&)>& onError) {
    uint8_t encoded[CrashCodec::MAX_ENCODED_SIZE];
    const size_t encodedLength = CrashCodec::encode(entry, encoded, sizeof(encoded));
    if (encodedLength == 0) {
        onError({WirelessError::INVALID_STATE, "protobuf encode failed"});
        return;
    }

    HttpRequest request(POST_DEVICE_LOGS_ENDPOINT, "POST", "application/protobuf",
                        std::string(reinterpret_cast<const char*>(encoded), encodedLength),
                        onSuccess, onError);

    if (!wirelessManager->queueHttpRequest(std::move(request))) {
        onError({WirelessError::INVALID_STATE, "HTTP request not queued"});
    }
}

}  // namespace CrashApi
