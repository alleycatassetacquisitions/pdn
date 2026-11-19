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
 * Told how a by-band lookup ended. @p player holds the decoded profile and
 * @p recordKey the decoded key; both are empty unless @p result is KEY_ISSUED
 * or KEY_NOT_ISSUED, and both live only for the call: the key is wiped after it.
 */
using OnBandLookupDone = std::function<void(ProvisionLookupResult result, const Player& player,
                                            const RecordKey& recordKey)>;

/**
 * GET /device/players/by-band/{uid}, {uid} being @p uid as upper-case hex with
 * no separators; calls @p onDone once with how it ended. The request has no
 * deadline of its own: a caller that must not wait on a lost Wi-Fi link keeps
 * one, and ignores a later answer. The UID is never logged: logs show only the
 * route (loggablePathLength).
 */
inline void getDevicePlayerByBand(WirelessManager* wirelessManager, const BandUid& uid,
                                  const OnBandLookupDone& onDone) {
    if (!uid.hasLookupLength()) {
        LOG_E("PLAYER_API", "Band UID of %u bytes not looked up",
              static_cast<unsigned>(uid.length));
        onDone(ProvisionLookupResult::LOOKUP_FAILED, Player(), RecordKey());
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
    [onDone](const std::string& response) {
        Player player;
        RecordKey recordKey;
        if (!DevicePlayerCodec::decode(response, player, recordKey)) {
            LOG_E("PLAYER_API", "Band lookup answer not decodable");
            onDone(ProvisionLookupResult::LOOKUP_FAILED, Player(), recordKey);
            return;
        }
        onDone(recordKey.isHeld() ? ProvisionLookupResult::KEY_ISSUED
                                  : ProvisionLookupResult::KEY_NOT_ISSUED,
               player, recordKey);
    },
    [onDone](const WirelessErrorInfo& error) {
        ProvisionLookupResult result = ProvisionLookupResult::LOOKUP_FAILED;
        if (error.code == WirelessError::SERVER_NOT_TRUSTED) {
            result = ProvisionLookupResult::SERVER_NOT_TRUSTED;
        } else if (error.httpStatus == 404) {
            result = ProvisionLookupResult::NO_PLAYER;
        }
        onDone(result, Player(), RecordKey());
    });
    request.loggablePathLength = sizeof(GET_DEVICE_PLAYER_BY_BAND_ENDPOINT) - 1;
    request.requestHeaders.push_back({"Accept", "application/protobuf"});

    // A request the manager or client refuses has already failed through its error callback.
    wirelessManager->queueHttpRequest(std::move(request));
}

}  // namespace PlayerApi
