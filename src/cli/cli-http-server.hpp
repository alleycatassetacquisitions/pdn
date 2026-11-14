#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <map>
#include <functional>
#include <cstdio>
#include "cli/cli-device-player-response-encode.hpp"
#include "utils/uuid-bytes.hpp"

namespace cli {

/**
 * Player configuration for mock responses.
 */
struct MockPlayerConfig {
    std::string name;
    UUIDBytes wirePlayerId;
};

/**
 * Mock HTTP Server singleton for CLI simulator.
 * Handles HTTP requests and returns mock responses.
 */
class MockHttpServer {
public:
    static MockHttpServer& getInstance() {
        static MockHttpServer instance;
        return instance;
    }
    
    // Prevent copying
    MockHttpServer(const MockHttpServer&) = delete;
    MockHttpServer& operator=(const MockHttpServer&) = delete;

    /**
     * Configure the player the by-band route answers for @p bandUid, the
     * upper-case hex path segment of the lookup.
     */
    void configurePlayer(const std::string& bandUid, const MockPlayerConfig& config) {
        playerConfigs[bandUid] = config;
    }

    /**
     * Remove the player configured for @p bandUid.
     */
    void removePlayer(const std::string& bandUid) { playerConfigs.erase(bandUid); }

    /**
     * Process an HTTP request and return response.
     *
     * @param method HTTP method (GET, POST, PUT, DELETE)
     * @param path Request path (e.g., "/device/players/by-band/04A21B9C")
     * @param body Request body (for POST/PUT)
     * @param responseBody Output: response body
     * @return HTTP status code (200, 404, 500, etc.)
     */
    int handleRequest(const std::string& method, const std::string& path, const std::string& body,
                      std::string& responseBody) {
        if (method == "GET" && path.find(DEVICE_PLAYER_BY_BAND_PREFIX) == 0) {
            return handleGetDevicePlayerByBand(path, responseBody);
        }
        if (method == "POST" && path == "/device-logs") {
            return handlePostDeviceLogs(body, responseBody);
        }
        responseBody = R"({"errors":["Not found"]})";
        return 404;
    }

    /**
     * Set whether the server should simulate being offline.
     */
    void setOffline(bool offline) {
        isOffline_ = offline;
    }

    bool isOffline() const {
        return isOffline_;
    }

private:
    MockHttpServer() = default;

    static constexpr const char DEVICE_PLAYER_BY_BAND_PREFIX[] = "/device/players/by-band/";

    std::map<std::string, MockPlayerConfig> playerConfigs;
    bool isOffline_ = false;

    int handleGetDevicePlayerByBand(const std::string& path, std::string& responseBody) {
        const std::string bandUid = path.substr(sizeof(DEVICE_PLAYER_BY_BAND_PREFIX) - 1);
        std::map<std::string, MockPlayerConfig>::const_iterator it = playerConfigs.find(bandUid);
        if (it == playerConfigs.end()) {
            responseBody = R"({"errors":["Player not found"]})";
            return 404;
        }
        if (it->second.wirePlayerId.isZero()) {
            responseBody = R"({"errors":["Player not configured"]})";
            return 500;
        }
        if (!cli::DevicePlayerResponseEncode::encodeMinimal(it->second.wirePlayerId,
                                                            it->second.name.c_str(), responseBody)) {
            responseBody = R"({"errors":["Encode failed"]})";
            return 500;
        }
        return 200;
    }

    /**
     * Handle POST /device-logs — acknowledges any non-empty protobuf body with 204.
     */
    int handlePostDeviceLogs(const std::string& body, std::string& responseBody) {
        responseBody = "";
        if (body.empty()) {
            responseBody = R"({"errors":["Empty body"]})";
            return 400;
        }
        return 204;
    }
};

} // namespace cli

#endif // NATIVE_BUILD
