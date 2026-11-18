#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "alleycat-server/device-player-codec.hpp"
#include "device/drivers/band-reader.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

/** How a band lookup ended. Only KEY_ISSUED registers anyone (one delivery, #486). */
enum class ProvisionLookupResult : uint8_t {
    // The server sent a profile and a record key of ISSUED_RECORD_KEY_SIZE bytes.
    KEY_ISSUED,
    // The server sent a profile but no key of ISSUED_RECORD_KEY_SIZE bytes.
    KEY_NOT_ISSUED,
    // The server answered 404: no player is linked to the band. The server
    // cannot tell an unknown band from an unlinked one.
    NO_PLAYER,
    // No trusted server is configured, or the server presented a key other
    // than the pinned one, so nothing it sent was used.
    SERVER_NOT_TRUSTED,
    // Any other failure: transport, another status, an undecodable answer, or a
    // UID of a length anticollision never returns.
    LOOKUP_FAILED,
};

namespace PlayerApi {

constexpr const char GET_DEVICE_PLAYER_BY_BAND_ENDPOINT[] = "/device/players/by-band/";

/**
 * Classifies an error the by-band lookup reported: an untrusted server,
 * a 404, or anything else.
 */
inline ProvisionLookupResult classifyLookupError(const WirelessErrorInfo& error) {
    if (error.code == WirelessError::SERVER_NOT_TRUSTED) {
        return ProvisionLookupResult::SERVER_NOT_TRUSTED;
    }
    if (error.httpStatus == 404) {
        return ProvisionLookupResult::NO_PLAYER;
    }
    return ProvisionLookupResult::LOOKUP_FAILED;
}

/**
 * GET /device/players/by-band/{uid}, {uid} being @p uid as upper-case hex with
 * no separators. Decodes the answer into @p player's registration fields and
 * @p recordKey, then calls @p onDone with how the lookup ended; both must outlive
 * the request. The UID is never logged: the request shows logs only the route.
 */
inline void getDevicePlayerByBand(WirelessManager* wirelessManager, const BandUid& uid,
                                  Player* player, IssuedRecordKey* recordKey,
                                  const std::function<void(ProvisionLookupResult)>& onDone) {
    if (!uid.hasLookupLength()) {
        LOG_E("PLAYER_API", "Band UID of %u bytes not looked up",
              static_cast<unsigned>(uid.length));
        onDone(ProvisionLookupResult::LOOKUP_FAILED);
        return;
    }
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    std::string path(GET_DEVICE_PLAYER_BY_BAND_ENDPOINT);
    for (uint8_t i = 0; i < uid.length; ++i) {
        path += HEX_DIGITS[uid.bytes[i] >> 4];
        path += HEX_DIGITS[uid.bytes[i] & 0x0F];
    }

    HttpRequest request(
    std::move(path), "GET", "", "",
    [onDone, player, recordKey](const std::string& response) {
        if (!DevicePlayerCodec::decode(response, *player, *recordKey)) {
            LOG_E("PLAYER_API", "Band lookup answer not decodable");
            onDone(ProvisionLookupResult::LOOKUP_FAILED);
            return;
        }
        onDone(recordKey->held ? ProvisionLookupResult::KEY_ISSUED
                               : ProvisionLookupResult::KEY_NOT_ISSUED);
    },
    [onDone](const WirelessErrorInfo& error) { onDone(classifyLookupError(error)); });
    request.loggablePathLength = sizeof(GET_DEVICE_PLAYER_BY_BAND_ENDPOINT) - 1;
    request.requestHeaders.push_back({"Accept", "application/protobuf"});

    // A refusal has already reported SERVER_NOT_TRUSTED through the error callback.
    wirelessManager->queueHttpRequest(std::move(request));
}

}  // namespace PlayerApi
