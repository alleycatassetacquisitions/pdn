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
     * Configure a player's data for mock responses.
     * Call this when creating a device to set up its player data.
     */
    void configurePlayer(const std::string& playerId, const MockPlayerConfig& config) {
        playerConfigs_[playerId] = config;
    }
    
    /**
     * Remove a player's configuration.
     */
    void removePlayer(const std::string& playerId) {
        playerConfigs_.erase(playerId);
    }
    
    /**
     * Process an HTTP request and return response.
     * 
     * @param method HTTP method (GET, POST, PUT, DELETE)
     * @param path Request path (e.g., "/device/players/by-pin/0010")
     * @param body Request body (for POST/PUT)
     * @param responseBody Output: response body
     * @return HTTP status code (200, 404, 500, etc.)
     */
    int handleRequest(const std::string& method, const std::string& path, const std::string& body,
                      std::string& responseBody) {
        if (method == "GET" && path.find("/device/players/by-pin/") == 0) {
            return handleGetDevicePlayerByPin(path, responseBody);
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

    std::map<std::string, MockPlayerConfig> playerConfigs_;
    bool isOffline_ = false;

    int handleGetDevicePlayerByPin(const std::string& path, std::string& responseBody) {
        const std::string prefix = "/device/players/by-pin/";
        if (path.find(prefix) != 0) {
            responseBody = R"({"errors":["Invalid path"]})";
            return 400;
        }
        const std::string pin = path.substr(prefix.length());
        auto it = playerConfigs_.find(pin);
        if (it == playerConfigs_.end()) {
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
