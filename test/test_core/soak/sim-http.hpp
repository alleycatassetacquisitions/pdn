#pragma once

#include <ArduinoJson.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <sys/socket.h>
#include <unistd.h>

#include "alleycat-server/device_api.pb.h"
#include "device/drivers/driver-interface.hpp"
#include "utils/simple-timer.hpp"
#include "utils/uuid-bytes.hpp"

#include <cstdint>
#include <ctime>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace soak {

struct HttpResult {
    bool transportOk = false;
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

// Faults one FDN's HTTP path suffers. offline: Wi-Fi never associates.
struct HttpFaults {
    double serverError = 0;
    double transportFail = 0;
    double lostResponse = 0;
    bool offline = false;
};

// HttpClientInterface with the ESP32 driver's observable behaviour
// (esp32-s3-http-client-driver.hpp): Wi-Fi association before the first request,
// one request in flight, 2xx = success, other statuses fail at once as
// SERVER_ERROR (>= 500) or INVALID_RESPONSE with the status, transport failures
// and the 15 s timeout retried once after 2 s, and disconnect() failing
// everything queued.
class SimHttpClient : public HttpClientDriverInterface {
public:
    static constexpr unsigned long REQUEST_TIMEOUT_MS = 15000;
    static constexpr unsigned long WIFI_CONNECTION_TIMEOUT_MS = 12500;
    static constexpr int MAX_RETRIES = 1;

    /** Requests go to @p backend; @p faults and @p rng belong to the FDN's world slot. */
    SimHttpClient(const std::string& name, HttpBackend& backend, HttpFaults& faults,
                  std::mt19937_64& rng)
        : HttpClientDriverInterface(name)
        , backend(backend)
        , faults(faults)
        , rng(rng) {}

    /** Nothing to bring up until Wi-Fi mode. */
    int initialize() override { return 0; }

    /** Associates, then runs the front request. Called with this FDN's clock installed. */
    void exec() override {
        if (state != HttpClientState::CONNECTED) return;
        const unsigned long now = SimpleTimer::getPlatformClock()->milliseconds();
        if (!wifiConnected) {
            if (!faults.offline && now >= connectAt) wifiConnected = true;
            return;
        }
        if (queue.empty()) return;
        HttpRequest& request = queue.front();
        if (!request.inProgress) {
            if (retryAt != 0 && static_cast<long>(now - retryAt) < 0) return;
            request.inProgress = true;
            request.lastAttemptTime = now;
            attempt(request, now);
            return;
        }
        if (now - request.lastAttemptTime > REQUEST_TIMEOUT_MS) {
            endAttempt({WirelessError::TIMEOUT, "Request timed out"});
        } else if (static_cast<long>(now - completeAt) >= 0) {
            finish();
        }
    }

    /** Ignored: the backend fixes the base URL. */
    void setWifiConfig(WifiConfig* config) override { (void)config; }
    /** Ignored. */
    void updateConfig(WifiConfig* config) override { (void)config; }
    /** Ignored: association is retried on the next CONNECTED. */
    void retryConnection() override {}
    /** Associated with the access point. */
    bool isConnected() override { return wifiConnected; }

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

    /** No station MAC is modelled. */
    uint8_t* getMacAddress() override { return mac; }

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

    /** CONNECTED or DISCONNECTED. */
    HttpClientState getHttpClientState() override { return state; }

private:
    bool chance(double p) { return p > 0 && std::uniform_real_distribution<double>(0, 1)(rng) < p; }

    void attempt(HttpRequest& request, unsigned long now) {
        completeAt = now + std::uniform_int_distribution<unsigned long>(20, 400)(rng);
        if (chance(faults.transportFail)) {
            result = {false, 0, ""};
            return;
        }
        if (chance(faults.serverError)) {
            result = {true, 503, "injected"};
            return;
        }
        result =
        backend.perform(request.method, request.path, request.contentType, request.payload);
        // The server acted, but its answer never comes back: the timeout path.
        if (chance(faults.lostResponse)) completeAt = now + REQUEST_TIMEOUT_MS + 1000;
    }

    void finish() {
        if (!result.transportOk) {
            endAttempt({WirelessError::CONNECTION_FAILED, "transport failed"});
            return;
        }
        queue.front().responseData = result.body;
        if (result.status >= 200 && result.status < 300) {
            completeFront(true, {});
            return;
        }
        const WirelessError type =
        result.status >= 500 ? WirelessError::SERVER_ERROR : WirelessError::INVALID_RESPONSE;
        completeFront(false, {type, "HTTP Error: " + std::to_string(result.status), result.status});
    }

    void endAttempt(const WirelessErrorInfo& error) {
        HttpRequest& request = queue.front();
        request.inProgress = false;
        if (request.retryCount < MAX_RETRIES) {
            request.retryCount++;
            retryAt =
            SimpleTimer::getPlatformClock()->milliseconds() + (1UL << request.retryCount) * 1000;
            return;
        }
        completeFront(false, error);
    }

    void completeFront(bool success, const WirelessErrorInfo& error) {
        HttpRequest done = std::move(queue.front());
        queue.pop();
        retryAt = 0;
        if (success && done.onSuccess) {
            done.onSuccess(done.responseData);
        } else if (!success && done.onError) {
            done.onError(error);
        }
    }

    HttpBackend& backend;
    HttpFaults& faults;
    std::mt19937_64& rng;
    std::queue<HttpRequest> queue;
    HttpClientState state = HttpClientState::DISCONNECTED;
    bool wifiConnected = false;
    unsigned long connectAt = 0;
    unsigned long retryAt = 0;
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

/** The event-scoped server surface the harness seeds and the checker reads. */
class EventServer {
public:
    /** Servers are owned by the world. */
    virtual ~EventServer() = default;
    /** Creates and activates a fresh event and turns scoring on. */
    virtual bool openEvent(const std::string& name) = 0;
    /** Adds a team to the event. */
    virtual bool addTeam(const std::string& name, UUIDBytes& id) = 0;
    /** Registers a player on @p team. */
    virtual bool addPlayer(const std::string& name, const UUIDBytes& team, UUIDBytes& id) = 0;
    /** Every accepted match of the event. */
    virtual bool listMatches(std::vector<ServerRow>& rows) = 0;
    /** Raw report count per match id (every retry and copy). */
    virtual bool countReports(std::map<std::string, int>& counts) = 0;
    /** Where FDN uploads go. */
    virtual HttpBackend& deviceBackend() = 0;
};

inline std::string formatUuid(const UUIDBytes& id) {
    char text[UUIDBytes::TEXT_BUFFER_SIZE];
    id.format(text);
    return text;
}

// In-process model of alleycat-rs POST /quickdraw-matches (routes/quickdraw.rs
// at a01a3fd): one active event, every entry kept as a raw report, entries
// validated in the server's order, first accepted report wins, 204 when all are
// accepted and 200 with protobuf rejections otherwise.
class FakeServer : public EventServer, public HttpBackend {
public:
    /** Player and team ids come from @p seed, so a run replays. */
    explicit FakeServer(uint64_t seed)
        : rng(seed) {}

    /** Starts the one event. */
    bool openEvent(const std::string& name) override {
        (void)name;
        eventActive = true;
        return true;
    }

    /** Adds a team with a fresh v4 id. */
    bool addTeam(const std::string& name, UUIDBytes& id) override {
        (void)name;
        id = freshId();
        teams.insert(formatUuid(id));
        return true;
    }

    /** Registers a player with a fresh v4 id. */
    bool addPlayer(const std::string& name, const UUIDBytes& team, UUIDBytes& id) override {
        (void)name;
        (void)team;
        id = freshId();
        players.insert(formatUuid(id));
        return true;
    }

    /** Accepted rows. */
    bool listMatches(std::vector<ServerRow>& out) override {
        for (const std::pair<const std::string, ServerRow>& row : rows)
            out.push_back(row.second);
        return true;
    }

    /** Raw reports per id. */
    bool countReports(std::map<std::string, int>& counts) override {
        counts = reports;
        return true;
    }

    /** This object. */
    HttpBackend& deviceBackend() override { return *this; }

    /** Handles POST /quickdraw-matches; anything else is a 404. */
    HttpResult perform(const std::string& method, const std::string& path,
                       const std::string& contentType, const std::string& body) override {
        if (method != "POST" || path != "/quickdraw-matches") return {true, 404, "Not Found"};
        if (contentType != "application/protobuf") return {true, 415, "Unsupported Media Type"};
        if (!eventActive) return {true, 409, "No active event"};
        std::vector<alleycat_device_QuickdrawMatch> batch;
        alleycat_device_WriteQuickdrawMatchesRequest request =
        alleycat_device_WriteQuickdrawMatchesRequest_init_zero;
        request.matches.funcs.decode = &FakeServer::decodeMatch;
        request.matches.arg = &batch;
        pb_istream_t stream =
        pb_istream_from_buffer(reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
        if (!pb_decode(&stream, alleycat_device_WriteQuickdrawMatchesRequest_fields, &request)) {
            return {true, 400, "bad protobuf"};
        }
        if (batch.empty() || batch.size() > 100) return {true, 400, "Supply 1-100 matches"};
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
        if (rejected.empty()) return {true, 204, ""};
        return {true, 200, encodeRejections(rejected)};
    }

    // False answers 409 "No active event", as between events.
    bool eventActive = false;

private:
    using Code = alleycat_device_QuickdrawMatchErrorCode;

    static bool decodeMatch(pb_istream_t* stream, const pb_field_t* field, void** arg) {
        (void)field;
        alleycat_device_QuickdrawMatch match = alleycat_device_QuickdrawMatch_init_zero;
        if (!pb_decode(stream, alleycat_device_QuickdrawMatch_fields, &match)) return false;
        static_cast<std::vector<alleycat_device_QuickdrawMatch>*>(*arg)->push_back(match);
        return true;
    }

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
        const Code invalidUuid =
        alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_UUID;
        const Code reference =
        alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_INVALID_EVENT_REFERENCE;
        if (!isUuid(match.id)) return reject(out, invalidUuid, "id");
        if (!isUuid(match.player_a_id)) return reject(out, invalidUuid, "player_a_id");
        if (!isUuid(match.player_b_id)) return reject(out, invalidUuid, "player_b_id");
        if (match.winner_player_id[0] != '\0' && !isUuid(match.winner_player_id)) {
            return reject(out, invalidUuid, "winner_player_id");
        }
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
        if (!match.has_occurred_at_unix_ms || !match.has_player_a_draw_time_ms ||
            !match.has_player_b_draw_time_ms) {
            return reject(
            out, alleycat_device_QuickdrawMatchErrorCode_QUICKDRAW_MATCH_ERROR_CODE_MISSING_FIELD,
            "occurred_at_unix_ms");
        }
        if (players.count(match.player_a_id) == 0) return reject(out, reference, "player_a_id");
        if (players.count(match.player_b_id) == 0) return reject(out, reference, "player_b_id");
        if (match.has_player_a_team_id && teams.count(match.player_a_team_id) == 0) {
            return reject(out, reference, "player_a_team_id");
        }
        if (match.has_player_b_team_id && teams.count(match.player_b_team_id) == 0) {
            return reject(out, reference, "player_b_team_id");
        }
        if (rows.count(match.id) != 0) return true;
        ServerRow row;
        row.id = match.id;
        row.playerA = match.player_a_id;
        row.playerB = match.player_b_id;
        row.teamA = match.has_player_a_team_id ? match.player_a_team_id : "";
        row.teamB = match.has_player_b_team_id ? match.player_b_team_id : "";
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

// Blocking HTTP/1.1 over a POSIX socket, loopback only: the harness must never
// reach a server other than a local throwaway instance.
class LoopbackHttp : public HttpBackend {
public:
    /** @p baseUrl like http://127.0.0.1:8000; any other host leaves this unusable. */
    explicit LoopbackHttp(const std::string& baseUrl) {
        const std::string scheme = "http://";
        if (baseUrl.compare(0, scheme.size(), scheme) != 0) return;
        const std::string rest = baseUrl.substr(scheme.size());
        const size_t colon = rest.find(':');
        const std::string host = rest.substr(0, colon);
        if (host != "127.0.0.1" && host != "localhost") return;
        port = colon == std::string::npos ? 80 : std::atoi(rest.c_str() + colon + 1);
        usable = port > 0 && port < 65536;
    }

    /** False when the URL named anything but loopback. */
    bool isUsable() const { return usable; }

    /** One request on a fresh connection, Connection: close. */
    HttpResult perform(const std::string& method, const std::string& path,
                       const std::string& contentType, const std::string& body) override {
        HttpResult out;
        if (!usable) return out;
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return out;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        std::string request =
        method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" +
        "Connection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
        if (!contentType.empty()) request += "Content-Type: " + contentType + "\r\n";
        request += "\r\n" + body;
        std::string response;
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            sendAll(fd, request)) {
            char chunk[4096];
            ssize_t got = 0;
            while ((got = recv(fd, chunk, sizeof(chunk), 0)) > 0) {
                response.append(chunk, static_cast<size_t>(got));
            }
        }
        close(fd);
        const size_t headerEnd = response.find("\r\n\r\n");
        if (response.compare(0, 5, "HTTP/") != 0 || headerEnd == std::string::npos) return out;
        out.transportOk = true;
        out.status = std::atoi(response.c_str() + response.find(' ') + 1);
        out.body = response.substr(headerEnd + 4);
        std::string headers = response.substr(0, headerEnd);
        for (char& c : headers)
            c = static_cast<char>(std::tolower(c));
        if (headers.find("transfer-encoding: chunked") != std::string::npos) {
            out.body = dechunk(out.body);
        }
        return out;
    }

private:
    static bool sendAll(int fd, const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    static std::string dechunk(const std::string& body) {
        std::string out;
        size_t at = 0;
        while (at < body.size()) {
            const size_t lineEnd = body.find("\r\n", at);
            if (lineEnd == std::string::npos) break;
            const size_t size = std::strtoul(body.c_str() + at, nullptr, 16);
            if (size == 0) break;
            out += body.substr(lineEnd + 2, size);
            at = lineEnd + 2 + size + 2;
        }
        return out;
    }

    int port = 0;
    bool usable = false;
};

/** RFC 3339 UTC ("2026-11-18T18:47:25.613429Z") to Unix milliseconds. */
inline int64_t parseRfc3339Ms(const char* text) {
    std::tm parts{};
    int consumed = 0;
    if (sscanf(text, "%d-%d-%dT%d:%d:%d%n", &parts.tm_year, &parts.tm_mon, &parts.tm_mday,
               &parts.tm_hour, &parts.tm_min, &parts.tm_sec, &consumed) != 6) {
        return -1;
    }
    parts.tm_year -= 1900;
    parts.tm_mon -= 1;
    int64_t ms = static_cast<int64_t>(timegm(&parts)) * 1000;
    if (text[consumed] == '.') {
        int scale = 100;
        for (const char* p = text + consumed + 1; *p >= '0' && *p <= '9' && scale > 0; p++) {
            ms += (*p - '0') * scale;
            scale /= 10;
        }
    }
    return ms;
}

// The lead's local alleycat-rs (origin/main a01a3fd) over its admin JSON routes:
// a fresh event per seed so checks are event-scoped and no reset is needed.
class RealServer : public EventServer {
public:
    /** Talks to @p baseUrl, loopback only. */
    explicit RealServer(const std::string& baseUrl)
        : http(baseUrl) {}

    /** False when the URL is not loopback. */
    bool isUsable() const { return http.isUsable(); }

    /** POST /events, PUT /events/{id}/active, PUT /scoring. */
    bool openEvent(const std::string& name) override {
        JsonDocument doc;
        if (!call("POST", "/events", "{\"name\":\"" + name + "\"}", &doc)) return false;
        eventId = doc["id"].as<std::string>();
        return call("PUT", "/events/" + eventId + "/active", "", nullptr) &&
               call("PUT", "/scoring", "{\"enabled\":true}", nullptr);
    }

    /** POST /teams. */
    bool addTeam(const std::string& name, UUIDBytes& id) override {
        JsonDocument doc;
        return call("POST", "/teams", "{\"name\":\"" + name + "\"}", &doc) &&
               UUIDBytes::parse(doc["id"].as<const char*>(), id);
    }

    /** POST /players, then PATCH its team. */
    bool addPlayer(const std::string& name, const UUIDBytes& team, UUIDBytes& id) override {
        JsonDocument doc;
        if (!call("POST", "/players", "{\"name\":\"" + name + "\"}", &doc) ||
            !UUIDBytes::parse(doc["id"].as<const char*>(), id)) {
            return false;
        }
        return call("PATCH", "/players/" + formatUuid(id),
                    "{\"team_id\":\"" + formatUuid(team) + "\"}", nullptr);
    }

    /** GET /quickdraw-matches, every page. */
    bool listMatches(std::vector<ServerRow>& rows) override {
        for (int page = 1;; page++) {
            JsonDocument doc;
            if (!call("GET", "/quickdraw-matches?per_page=100&page=" + std::to_string(page), "",
                      &doc)) {
                return false;
            }
            JsonArray matches = doc["matches"].as<JsonArray>();
            for (JsonObject match : matches) {
                ServerRow row;
                row.id = match["id"].as<std::string>();
                row.playerA = match["player_a_id"].as<std::string>();
                row.playerB = match["player_b_id"].as<std::string>();
                row.teamA = match["player_a_team_id"] | "";
                row.teamB = match["player_b_team_id"] | "";
                row.winner = match["winner_player_id"] | "";
                row.drawA = match["player_a_draw_time_ms"].as<uint64_t>();
                row.drawB = match["player_b_draw_time_ms"].as<uint64_t>();
                row.occurredAtMs = parseRfc3339Ms(match["occurred_at"] | "");
                rows.push_back(row);
            }
            if (matches.size() < 100) return true;
        }
    }

    /** GET /quickdraw-matches/reports, every page. */
    bool countReports(std::map<std::string, int>& counts) override {
        for (int page = 1;; page++) {
            JsonDocument doc;
            if (!call("GET", "/quickdraw-matches/reports?per_page=100&page=" + std::to_string(page),
                      "", &doc)) {
                return false;
            }
            JsonArray reports = doc["reports"].as<JsonArray>();
            for (JsonObject report : reports)
                counts[report["match_id"] | ""]++;
            if (reports.size() < 100) return true;
        }
    }

    /** The loopback client. */
    HttpBackend& deviceBackend() override { return http; }

    std::string eventId;

private:
    // Admin routes answer 200, 201 or 204 by route; any 2xx is success.
    bool call(const std::string& method, const std::string& path, const std::string& body,
              JsonDocument* doc) {
        const HttpResult result =
        http.perform(method, path, body.empty() ? "" : "application/json", body);
        if (!result.transportOk || result.status / 100 != 2) {
            fprintf(stderr, "soak: %s %s -> %d %s\n", method.c_str(), path.c_str(), result.status,
                    result.body.c_str());
            return false;
        }
        return doc == nullptr || !deserializeJson(*doc, result.body);
    }

    LoopbackHttp http;
};

}  // namespace soak
