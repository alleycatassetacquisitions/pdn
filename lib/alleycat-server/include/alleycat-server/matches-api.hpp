#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include "alleycat-server/device_api.pb.h"
#include "alleycat-server/quickdraw-match-codec.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/wireless-types.hpp"

// A full batch rejected record by record is the largest reply the server sends;
// each rejection costs a tag and a length byte on top of its body.
static_assert(QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH *
              (alleycat_device_QuickdrawMatchRejection_size + 2) <=
              MAX_HTTP_RESPONSE_BYTES,
              "a fully rejected batch would not fit the HTTP response bound");

namespace MatchesApi {

constexpr const char POST_QUICKDRAW_MATCHES_ENDPOINT[] = "/quickdraw-matches";

/**
 * POST /quickdraw-matches — uploads @p matchCount matches pulled from @p source as
 * protobuf. @p onRejection receives each rejected match, then @p onSuccess receives
 * the totals; an error leaves the outcome unknown, and retrying the same match IDs
 * is safe.
 *
 * @param matchCount  1 to QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH; otherwise
 *                    @p onError fires with INVALID_STATE and nothing is queued.
 */
inline void uploadQuickdrawMatches(
    WirelessManager* wirelessManager,
    size_t matchCount,
    const QuickdrawMatchCodec::MatchSource& source,
    const QuickdrawMatchCodec::RejectionSink& onRejection,
    const std::function<void(const QuickdrawUploadResult&)>& onSuccess,
    const std::function<void(const WirelessErrorInfo&)>& onError) {
    if (matchCount == 0 || matchCount > QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH) {
        onError({WirelessError::INVALID_STATE, "match batch must hold 1-100 matches"});
        return;
    }

    std::string payload;
    if (!QuickdrawMatchCodec::encodeRequest(matchCount, source, payload)) {
        onError({WirelessError::INVALID_STATE, "protobuf encode failed"});
        return;
    }

    HttpRequest request(
        POST_QUICKDRAW_MATCHES_ENDPOINT,
        "POST",
        "application/protobuf",
        std::move(payload),
        [onRejection, onSuccess, onError](const std::string& response) {
            QuickdrawUploadResult result;
            if (QuickdrawMatchCodec::decodeResponse(response, onRejection, result)) {
                onSuccess(result);
            } else {
                onError({WirelessError::INVALID_RESPONSE,
                         "Failed to decode match upload response"});
            }
        },
        onError);
    if (!wirelessManager->queueHttpRequest(std::move(request))) {
        onError({WirelessError::INVALID_STATE, "HTTP request not queued"});
    }
}

}  // namespace MatchesApi
