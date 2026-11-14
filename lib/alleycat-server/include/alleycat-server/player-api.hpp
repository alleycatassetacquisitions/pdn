#pragma once

#include <functional>
#include <string>

#include "alleycat-server/device-player-codec.hpp"
#include "device/drivers/band-reader.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-types.hpp"
#include "game/record-key.hpp"
#include "wireless/wireless-types.hpp"

/** How a band lookup ended, for the FDN's registration screen. */
enum class ProvisionLookupResult : uint8_t {
    // The server answered 404 or 400: no player is linked to the band, or the
    // UID is malformed. The server cannot tell an unknown band from an unlinked one.
    NO_PLAYER,
    // No answer within PROVISION_LOOKUP_TIMEOUT_MS, or the request failed in
    // transport (Wi-Fi, connection, timeout).
    SERVER_UNREACHABLE,
    // The server presented a key other than the one pinned in this FDN's
    // firmware, or the firmware pins none, so nothing it sent was used.
    SERVER_NOT_TRUSTED,
    // Any other failure: another error status, an undecodable answer, a request
    // that could not be queued, or a lookup cancelled before the server answered.
    LOOKUP_FAILED,
    // The server gave a profile, but no usable key was written to the PDN bound
    // at the tap: none was issued, the PDN left the jack, or the lookup was cancelled.
    KEY_NOT_ISSUED,
    // The record key was written to the PDN's jack and the profile sent after it.
    PROFILE_DELIVERED,
};

namespace PlayerApi {

constexpr const char GET_DEVICE_PLAYER_BY_BAND_ENDPOINT[] = "/device/players/by-band/";

/**
 * GET /device/players/by-band/{uid}, {uid} being @p uid as upper-case hex with no
 * separators — decodes protobuf into @p player's registration fields and hands
 * the player's record key to @p recordKey. On success @p recordKey holds a key
 * only when the response carried one of RECORD_KEY_SIZE bytes and came over HTTPS
 * from a server that presented the pinned key. On a decode error it holds none;
 * any other error leaves it as it was. A server presenting another key, or any
 * HTTPS server when no pin is configured, fails the lookup with
 * WirelessError::SERVER_NOT_TRUSTED. A UID that is not 4 or 7 bytes
 * fails with WirelessError::INVALID_STATE and queues nothing.
 * The server answers 400 for a malformed UID and 404 when no player is linked to it.
 */
inline void getDevicePlayerByBand(WirelessManager* wirelessManager, const BandUid& uid,
                                  Player* player, RecordKey* recordKey,
                                  const std::function<void(Player&)>& onSuccess,
                                  const std::function<void(const WirelessErrorInfo&)>& onError) {
    // The UID becomes a URL path segment; hex of a known length is all that leaves the FDN.
    if (!uid.hasLookupLength()) {
        LOG_E("PLAYER_API", "Band UID of %u bytes not looked up",
              static_cast<unsigned>(uid.length));
        onError({WirelessError::INVALID_STATE, "Band UID must be 4 or 7 bytes"});
        return;
    }
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    std::string path(GET_DEVICE_PLAYER_BY_BAND_ENDPOINT);
    path.reserve(path.size() + 2 * uid.length);
    for (uint8_t i = 0; i < uid.length; ++i) {
        path += HEX_DIGITS[uid.bytes[i] >> 4];
        path += HEX_DIGITS[uid.bytes[i] & 0x0F];
    }

    HttpRequest request(
    std::move(path), "GET", "", "",
    [onSuccess, onError, player, recordKey, wirelessManager](const std::string& response) {
        if (player == nullptr || recordKey == nullptr ||
            !DevicePlayerCodec::decode(response, *player, *recordKey)) {
            onError({WirelessError::INVALID_RESPONSE, "Failed to decode device player response"});
            return;
        }
        // Anyone on the path of a response from a server that did not present
        // the pinned key could have read the key or put their own in its place.
        if (recordKey->isHeld() && !wirelessManager->isHttpResponseServerVerified()) {
            LOG_E("PLAYER_API", "Record key refused: the server did not present the pinned key");
            recordKey->clear();
        }
        onSuccess(*player);
    },
    onError);

    request.requestHeaders.push_back({"Accept", "application/protobuf"});

    if (!wirelessManager->queueHttpRequest(std::move(request))) {
        onError({WirelessError::INVALID_STATE, "HTTP request not queued"});
    }
}

/**
 * Classifies an error getDevicePlayerByBand passed to its error callback by
 * this route's status contract: 404 or 400 is NO_PLAYER, a transport failure
 * SERVER_UNREACHABLE, an unpinned server key SERVER_NOT_TRUSTED, anything else
 * LOOKUP_FAILED.
 */
inline ProvisionLookupResult classifyLookupError(const WirelessErrorInfo& error) {
    if (error.code == WirelessError::SERVER_NOT_TRUSTED) {
        return ProvisionLookupResult::SERVER_NOT_TRUSTED;
    }
    if (error.httpStatus == 404 || error.httpStatus == 400) {
        return ProvisionLookupResult::NO_PLAYER;
    }
    if (error.code == WirelessError::TIMEOUT || error.code == WirelessError::CONNECTION_FAILED ||
        error.code == WirelessError::WIFI_NOT_CONNECTED) {
        return ProvisionLookupResult::SERVER_UNREACHABLE;
    }
    return ProvisionLookupResult::LOOKUP_FAILED;
}

}  // namespace PlayerApi
