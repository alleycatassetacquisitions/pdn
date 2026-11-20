#pragma once

#include <pb_decode.h>
#include <pb_encode.h>

#include "alleycat-server/device_api.pb.h"
#include "device/drivers/driver-interface.hpp"
#include "utils/simple-timer.hpp"
#include "utils/uuid-bytes.hpp"

#include <cstdint>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace soak {

struct HttpResult {
    int status = 0;
    std::string body;
};

/** Where a request goes: the in-process fake or a real local server. */
class HttpBackend {
public:
    /** Backends are owned by the world. */
    virtual ~HttpBackend() = default;
    /** Performs one request to completion; virtual time stands still meanwhile. */
    virtual HttpResult perform(const std::string& method, const std::string& path,
                               const std::string& contentType, const std::string& body) = 0;
};

inline bool decodeMatchEntry(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    alleycat_device_QuickdrawMatch match = alleycat_device_QuickdrawMatch_init_zero;
    if (!pb_decode(stream, alleycat_device_QuickdrawMatch_fields, &match)) return false;
    static_cast<std::vector<alleycat_device_QuickdrawMatch>*>(*arg)->push_back(match);
    return true;
}

/** Decodes a WriteQuickdrawMatchesRequest body into @p batch; false on bad protobuf. */
inline bool decodeMatchBatch(const std::string& body,
                             std::vector<alleycat_device_QuickdrawMatch>& batch) {
    alleycat_device_WriteQuickdrawMatchesRequest request =
    alleycat_device_WriteQuickdrawMatchesRequest_init_zero;
    request.matches.funcs.decode = &decodeMatchEntry;
    request.matches.arg = &batch;
    pb_istream_t stream =
    pb_istream_from_buffer(reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
    return pb_decode(&stream, alleycat_device_WriteQuickdrawMatchesRequest_fields, &request);
}

// HttpClientInterface with the ESP32 driver's observable behaviour
// (esp32-s3-http-client-driver.hpp): Wi-Fi association before the first request,
// one request in flight, 2xx = success, other statuses fail at once as
// SERVER_ERROR (>= 500) or INVALID_RESPONSE with the status, and disconnect()
// failing everything queued.
class SimHttpClient : public HttpClientDriverInterface {
public:
    /** Requests go to @p backend; @p rng, which times them, belongs to the FDN's world slot. */
    SimHttpClient(const std::string& name, HttpBackend& backend, std::mt19937_64& rng)
        : HttpClientDriverInterface(name)
        , backend(backend)
        , rng(rng) {}

    /** Nothing to bring up until Wi-Fi mode. */
    int initialize() override { return 0; }

    /** Associates, then runs the front request. Called with this FDN's clock installed. */
    void exec() override {
        if (state != HttpClientState::CONNECTED) return;
        const unsigned long now = SimpleTimer::getPlatformClock()->milliseconds();
        if (!wifiConnected) {
            if (now >= connectAt) wifiConnected = true;
            return;
        }
        if (queue.empty()) return;
        HttpRequest& request = queue.front();
        if (!request.inProgress) {
            request.inProgress = true;
            attempt(request, now);
            return;
        }
        if (static_cast<long>(now - completeAt) >= 0) finish();
    }

    /** Ignored: the backend fixes the base URL. */
    void setWifiConfig(WifiConfig* config) override { (void)config; }
    /** Ignored. */
    void updateConfig(WifiConfig* config) override { (void)config; }
    /** Ignored: association is retried on the next CONNECTED. */
    void retryConnection() override {}
    /** Associated with the access point. */
    bool isConnected() override { return wifiConnected; }
    /** No station MAC is modelled. */
    uint8_t* getMacAddress() override { return mac; }
    /** CONNECTED or DISCONNECTED. */
    HttpClientState getHttpClientState() override { return state; }

    /** Takes ownership of @p request. */
    bool queueRequest(HttpRequest&& request) override {
        queue.push(std::move(request));
        return true;
    }

    /** Leaves the access point and fails everything queued, as the ESP32 driver does. */
    void disconnect() override {
        while (!queue.empty()) {
            completeFront(false,
                          {WirelessError::CONNECTION_FAILED, "Disconnected before an answer"});
        }
        wifiConnected = false;
        state = HttpClientState::DISCONNECTED;
    }

    /** CONNECTED starts association; DISCONNECTED tears down. */
    void setHttpClientState(HttpClientState newState) override {
        if (newState == HttpClientState::CONNECTED && state != HttpClientState::CONNECTED) {
            connectAt = SimpleTimer::getPlatformClock()->milliseconds() +
                        std::uniform_int_distribution<unsigned long>(300, 2500)(rng);
        } else if (newState == HttpClientState::DISCONNECTED &&
                   state != HttpClientState::DISCONNECTED) {
            disconnect();
        }
        state = newState;
    }

private:
    void attempt(HttpRequest& request, unsigned long now) {
        completeAt = now + std::uniform_int_distribution<unsigned long>(20, 400)(rng);
        result =
        backend.perform(request.method, request.path, request.contentType, request.payload);
    }

    void finish() {
        queue.front().responseData = result.body;
        if (result.status >= 200 && result.status < 300) {
            completeFront(true, {});
            return;
        }
        const WirelessError type =
        result.status >= 500 ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE;
        completeFront(false, {type, "HTTP Error: " + std::to_string(result.status), result.status});
    }

    void completeFront(bool success, const WirelessErrorInfo& error) {
        HttpRequest done = std::move(queue.front());
        queue.pop();
        if (success && done.onSuccess) {
            done.onSuccess(done.responseData);
        } else if (!success && done.onError) {
            done.onError(error);
        }
    }

    HttpBackend& backend;
    std::mt19937_64& rng;
    std::queue<HttpRequest> queue;
    HttpClientState state = HttpClientState::DISCONNECTED;
    bool wifiConnected = false;
    unsigned long connectAt = 0;
    unsigned long completeAt = 0;
    HttpResult result;
    uint8_t mac[6] = {};
};

/** One accepted match as the server holds it. */
struct ServerRow {
    std::string id;
    std::string playerA;
    std::string playerB;
    std::string teamA;
    std::string teamB;
    std::string winner;
    uint64_t drawA = 0;
    uint64_t drawB = 0;
    int64_t occurredAtMs = 0;
};

inline std::string formatUuid(const UUIDBytes& id) {
    char text[UUIDBytes::TEXT_BUFFER_SIZE];
    id.format(text);
    return text;
}

// In-process model of alleycat-rs POST /quickdraw-matches (routes/quickdraw.rs
// at a01a3fd): one active event, every entry kept as a raw report, entries
// validated in the server's order, first accepted report wins (a duplicate skips
// the reference check, as ON CONFLICT DO NOTHING does), 204 when all are accepted
// and 200 with protobuf rejections otherwise.
class FakeServer : public HttpBackend {
public:
    /** Player and team ids come from @p seed, so a run replays. */
    explicit FakeServer(uint64_t seed)
        : rng(seed) {}

    /** Adds a team with a fresh v4 id. */
    bool addTeam(const std::string& name, UUIDBytes& id) {
        (void)name;
        id = freshId();
        teams.insert(formatUuid(id));
        return true;
    }

    /** Registers a player with a fresh v4 id. */
    bool addPlayer(const std::string& name, const UUIDBytes& team, UUIDBytes& id) {
        (void)name;
        (void)team;
        id = freshId();
        players.insert(formatUuid(id));
        return true;
    }

    /** Accepted rows. */
    bool listMatches(std::vector<ServerRow>& out) {
        for (const std::pair<const std::string, ServerRow>& row : rows)
            out.push_back(row.second);
        return true;
    }

    /** Raw reports per id. */
    bool countReports(std::map<std::string, int>& counts) {
        counts = reports;
        return true;
    }

    /** Handles POST /quickdraw-matches; anything else is a 404. */
    HttpResult perform(const std::string& method, const std::string& path,
                       const std::string& contentType, const std::string& body) override {
        if (method != "POST" || path != "/quickdraw-matches") return {404, "Not Found"};
        if (contentType != "application/protobuf") return {415, "Unsupported Media Type"};
        std::vector<alleycat_device_QuickdrawMatch> batch;
        if (!decodeMatchBatch(body, batch)) return {400, "bad protobuf"};
        if (batch.empty() || batch.size() > 100) return {400, "Supply 1-100 matches"};
        std::vector<alleycat_device_QuickdrawMatchRejection> rejected;
        for (size_t i = 0; i < batch.size(); i++) {
            reports[batch[i].id]++;
            alleycat_device_QuickdrawMatchRejection rejection =
            alleycat_device_QuickdrawMatchRejection_init_zero;
            if (!accept(batch[i], rejection)) {
                rejection.match_index = static_cast<uint32_t>(i);
                strncpy(rejection.match_id, batch[i].id, sizeof(rejection.match_id) - 1);
                rejected.push_back(rejection);
            }
        }
        if (rejected.empty()) return {204, ""};
        return {200, encodeRejections(rejected)};
    }

private:
    using Code = alleycat_device_QuickdrawMatchErrorCode;
    static constexpr Code INVALID_UUID =
    alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_UUID;
    static constexpr Code REFERENCE =
    alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_EVENT_REFERENCE;
    static constexpr Code MISSING =
    alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_MISSING_FIELD;
    static constexpr Code RANGE =
    alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_OUT_OF_RANGE;

    static bool encodeRejected(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
        for (const alleycat_device_QuickdrawMatchRejection& rejection :
             *static_cast<const std::vector<alleycat_device_QuickdrawMatchRejection>*>(*arg)) {
            if (!pb_encode_tag_for_field(stream, field) ||
                !pb_encode_submessage(stream, alleycat_device_QuickdrawMatchRejection_fields,
                                      &rejection)) {
                return false;
            }
        }
        return true;
    }

    static std::string encodeRejections(std::vector<alleycat_device_QuickdrawMatchRejection>& all) {
        alleycat_device_WriteQuickdrawMatchesResponse response =
        alleycat_device_WriteQuickdrawMatchesResponse_init_zero;
        response.rejected.funcs.encode = &FakeServer::encodeRejected;
        response.rejected.arg = &all;
        std::vector<pb_byte_t> buffer(16384);
        pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
        pb_encode(&stream, alleycat_device_WriteQuickdrawMatchesResponse_fields, &response);
        return std::string(reinterpret_cast<const char*>(buffer.data()), stream.bytes_written);
    }

    static bool reject(alleycat_device_QuickdrawMatchRejection& out, Code code, const char* a,
                       const char* b = nullptr) {
        out.code = code;
        strncpy(out.fields[0], a, sizeof(out.fields[0]) - 1);
        out.fields_count = 1;
        if (b != nullptr) {
            strncpy(out.fields[1], b, sizeof(out.fields[1]) - 1);
            out.fields_count = 2;
        }
        return false;
    }

    static bool isUuid(const char* text) {
        UUIDBytes parsed;
        return UUIDBytes::parse(text, parsed);
    }

    bool accept(const alleycat_device_QuickdrawMatch& match,
                alleycat_device_QuickdrawMatchRejection& out) {
        if (!isUuid(match.id)) return reject(out, INVALID_UUID, "id");
        if (!isUuid(match.player_a_id)) return reject(out, INVALID_UUID, "player_a_id");
        if (!isUuid(match.player_b_id)) return reject(out, INVALID_UUID, "player_b_id");
        if (match.winner_player_id[0] != '\0' && !isUuid(match.winner_player_id))
            return reject(out, INVALID_UUID, "winner_player_id");
        if (strcmp(match.player_a_id, match.player_b_id) == 0) {
            return reject(
            out, alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_SAME_PLAYER,
            "player_a_id", "player_b_id");
        }
        if (match.winner_player_id[0] != '\0' &&
            strcmp(match.winner_player_id, match.player_a_id) != 0 &&
            strcmp(match.winner_player_id, match.player_b_id) != 0) {
            return reject(
            out, alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_WINNER,
            "winner_player_id");
        }
        if (!match.has_occurred_at_unix_ms) return reject(out, MISSING, "occurred_at_unix_ms");
        // 1970 through 9999 in Unix milliseconds (quickdraw.rs:114-122).
        if (match.occurred_at_unix_ms < 0 || match.occurred_at_unix_ms > 253402300799999LL)
            return reject(out, RANGE, "occurred_at_unix_ms");
        if (match.has_player_a_team_id && !isUuid(match.player_a_team_id))
            return reject(out, INVALID_UUID, "player_a_team_id");
        if (match.has_player_b_team_id && !isUuid(match.player_b_team_id))
            return reject(out, INVALID_UUID, "player_b_team_id");
        if (!match.has_player_a_draw_time_ms) return reject(out, MISSING, "player_a_draw_time_ms");
        if (!match.has_player_b_draw_time_ms) return reject(out, MISSING, "player_b_draw_time_ms");
        const uint64_t drawLimit = static_cast<uint64_t>(INT64_MAX);
        if (match.player_a_draw_time_ms > drawLimit)
            return reject(out, RANGE, "player_a_draw_time_ms");
        if (match.player_b_draw_time_ms > drawLimit)
            return reject(out, RANGE, "player_b_draw_time_ms");
        // ON CONFLICT DO NOTHING: a duplicate never reaches the foreign keys.
        if (rows.count(match.id) != 0) return true;
        if (players.count(match.player_a_id) == 0) return reject(out, REFERENCE, "player_a_id");
        if (players.count(match.player_b_id) == 0) return reject(out, REFERENCE, "player_b_id");
        ServerRow row;
        row.teamA = match.has_player_a_team_id ? match.player_a_team_id : "";
        row.teamB = match.has_player_b_team_id ? match.player_b_team_id : "";
        if (!row.teamA.empty() && teams.count(row.teamA) == 0)
            return reject(out, REFERENCE, "player_a_team_id");
        if (!row.teamB.empty() && teams.count(row.teamB) == 0)
            return reject(out, REFERENCE, "player_b_team_id");
        row.id = match.id;
        row.playerA = match.player_a_id;
        row.playerB = match.player_b_id;
        row.winner = match.winner_player_id;
        row.drawA = match.player_a_draw_time_ms;
        row.drawB = match.player_b_draw_time_ms;
        row.occurredAtMs = match.occurred_at_unix_ms;
        rows[row.id] = row;
        return true;
    }

    UUIDBytes freshId() {
        UUIDBytes id;
        for (size_t i = 0; i < UUIDBytes::SIZE; i++)
            id.bytes[i] = static_cast<uint8_t>(rng());
        id.bytes[6] = static_cast<uint8_t>((id.bytes[6] & 0x0F) | 0x40);
        id.bytes[8] = static_cast<uint8_t>((id.bytes[8] & 0x3F) | 0x80);
        return id;
    }

    std::mt19937_64 rng;
    std::set<std::string> players;
    std::set<std::string> teams;
    std::map<std::string, ServerRow> rows;
    std::map<std::string, int> reports;
};

}  // namespace soak
