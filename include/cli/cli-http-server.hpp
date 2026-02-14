#pragma once

#ifdef NATIVE_BUILD

#include <string>
#include <map>
#include <functional>
#include <deque>
#include <cstdio>
#include <regex>

namespace cli {

/**
 * HTTP request/response tracking for CLI display.
 */
struct HttpHistoryEntry {
    std::string method;
    std::string path;
    std::string requestBody;
    int statusCode;
    std::string responseBody;
    bool success;
};

/**
 * Player configuration for mock responses.
 */
struct MockPlayerConfig {
    std::string id;
    std::string name;
    bool isHunter;
    int allegiance;  // 0 = NONE, 1 = RESISTANCE, 2 = HUNTERS_GUILD
    std::string faction;
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
     * @param path Request path (e.g., "/api/players/0010")
     * @param body Request body (for POST/PUT)
     * @param responseBody Output: response body
     * @return HTTP status code (200, 404, 500, etc.)
     */
    int handleRequest(const std::string& method, 
                      const std::string& path, 
                      const std::string& body,
                      std::string& responseBody) {
        
        int statusCode = 500;
        responseBody = R"({"errors":["Internal server error"]})";
        
        // Route the request
        if (method == "GET" && path.find("/api/players/") == 0) {
            statusCode = handleGetPlayer(path, responseBody);
        } else if (method == "GET" && path.find("/api/progress") == 0) {
            statusCode = handleGetProgress(path, responseBody);
        } else if (method == "PUT" && path == "/api/matches") {
            statusCode = handlePutMatches(body, responseBody);
        } else if (method == "PUT" && path == "/api/progress") {
            statusCode = handlePutProgress(body, responseBody);
        } else {
            statusCode = 404;
            responseBody = R"({"errors":["Not found"]})";
        }
        
        // Track in history
        HttpHistoryEntry entry;
        entry.method = method;
        entry.path = path;
        entry.requestBody = body;
        entry.statusCode = statusCode;
        entry.responseBody = responseBody;
        entry.success = (statusCode >= 200 && statusCode < 300);
        addToHistory(entry);
        
        return statusCode;
    }
    
    /**
     * Get recent request history for CLI display.
     */
    const std::deque<HttpHistoryEntry>& getHistory() const {
        return history_;
    }
    
    /**
     * Clear request history.
     */
    void clearHistory() {
        history_.clear();
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
    
    /**
     * Set simulated response delay in milliseconds.
     */
    void setResponseDelay(unsigned long delayMs) {
        responseDelayMs_ = delayMs;
    }
    
    unsigned long getResponseDelay() const {
        return responseDelayMs_;
    }

private:
    MockHttpServer() = default;
    
    std::map<std::string, MockPlayerConfig> playerConfigs_;
    std::deque<HttpHistoryEntry> history_;
    static constexpr size_t MAX_HISTORY = 10;
    bool isOffline_ = false;
    unsigned long responseDelayMs_ = 0;
    
    void addToHistory(const HttpHistoryEntry& entry) {
        history_.push_back(entry);
        while (history_.size() > MAX_HISTORY) {
            history_.pop_front();
        }
    }
    
    /**
     * Extract player ID from path like "/api/players/0010"
     */
    std::string extractPlayerId(const std::string& path) {
        const std::string prefix = "/api/players/";
        if (path.find(prefix) == 0) {
            return path.substr(prefix.length());
        }
        return "";
    }
    
    /**
     * Handle GET /api/players/{id}
     */
    int handleGetPlayer(const std::string& path, std::string& responseBody) {
        std::string playerId = extractPlayerId(path);
        
        if (playerId.empty()) {
            responseBody = R"({"errors":["Invalid player ID"]})";
            return 400;
        }
        
        // Check for special test IDs
        if (playerId == "9999") {
            // Test bounty
            responseBody = R"({"data":{"id":"9999","name":"TestBounty","hunter":false,"allegiance":1,"faction":"Rebels"}})";
            return 200;
        }
        if (playerId == "8888") {
            // Test hunter
            responseBody = R"({"data":{"id":"8888","name":"TestHunter","hunter":true,"allegiance":2,"faction":"Guild"}})";
            return 200;
        }
        
        // Look up configured player
        auto it = playerConfigs_.find(playerId);
        if (it == playerConfigs_.end()) {
            // Player not found - return a default based on ID
            // For CLI, generate sensible defaults
            bool defaultHunter = (std::stoi(playerId) % 2 == 0);  // Even IDs are hunters
            char buf[512];
            snprintf(buf, sizeof(buf),
                R"({"data":{"id":"%s","name":"Player%s","hunter":%s,"allegiance":1,"faction":"Default"}})",
                playerId.c_str(),
                playerId.c_str(),
                defaultHunter ? "true" : "false");
            responseBody = buf;
            return 200;
        }
        
        // Return configured player data
        const MockPlayerConfig& config = it->second;
        char buf[512];
        snprintf(buf, sizeof(buf),
            R"({"data":{"id":"%s","name":"%s","hunter":%s,"allegiance":%d,"faction":"%s"}})",
            config.id.c_str(),
            config.name.c_str(),
            config.isHunter ? "true" : "false",
            config.allegiance,
            config.faction.c_str());
        responseBody = buf;
        return 200;
    }
    
    /**
     * Handle PUT /api/matches
     */
    int handlePutMatches(const std::string& body, std::string& responseBody) {
        // Just acknowledge the match upload
        // In a real server, we'd parse and store the matches
        responseBody = R"({"success":true,"message":"Matches uploaded"})";
        return 200;
    }

    /**
     * Handle GET /api/progress
     */
    int handleGetProgress(const std::string& path, std::string& responseBody) {
        // Return the last synced progress data
        if (lastProgressBody_.empty()) {
            // No progress data yet - return defaults
            responseBody = R"({"data":{"konami":0,"boon":false,"profile":-1,"colorEligibility":0,"easyAttempts":[0,0,0,0,0,0,0],"hardAttempts":[0,0,0,0,0,0,0]}})";
        } else {
            // Return the last uploaded progress wrapped in data object
            responseBody = "{\"data\":" + lastProgressBody_ + "}";
        }
        return 200;
    }

    /**
     * Handle PUT /api/progress
     */
    int handlePutProgress(const std::string& body, std::string& responseBody) {
        // Store the progress upload for later GET requests
        lastProgressBody_ = body;
        responseBody = R"({"success":true,"message":"Progress synced"})";
        return 200;
    }

public:
    /**
     * Get the last progress body received (for test verification).
     */
    const std::string& getLastProgressBody() const {
        return lastProgressBody_;
    }

private:
    std::string lastProgressBody_;
};

} // namespace cli

#endif // NATIVE_BUILD
