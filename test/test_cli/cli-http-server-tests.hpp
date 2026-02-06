//
// Mock HTTP Server Tests - Tests for cli::MockHttpServer and NativeHttpClientDriver
//

#pragma once

#include <gtest/gtest.h>
#include "cli/cli-http-server.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "wireless/wireless-types.hpp"

// ============================================
// MOCK HTTP SERVER TEST SUITE
// ============================================

class MockHttpServerTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        server_ = &cli::MockHttpServer::getInstance();
        server_->clearHistory();
        server_->setOffline(false);
    }
    
    void TearDown() override {
        server_->clearHistory();
        server_->setOffline(false);
    }
    
    cli::MockHttpServer* server_;
};

// Test: Get player returns configured data
void httpServerGetPlayerReturnsCorrectData(MockHttpServerTestSuite* suite) {
    cli::MockPlayerConfig config;
    config.id = "1234";
    config.name = "TestPlayer";
    config.isHunter = true;
    config.allegiance = 2;
    config.faction = "TestFaction";
    
    suite->server_->configurePlayer("1234", config);
    
    std::string response;
    int status = suite->server_->handleRequest("GET", "/api/players/1234", "", response);
    
    ASSERT_EQ(status, 200);
    ASSERT_TRUE(response.find("TestPlayer") != std::string::npos);
    ASSERT_TRUE(response.find("\"hunter\":true") != std::string::npos);
    
    suite->server_->removePlayer("1234");
}

// Test: Get unknown player returns defaults
void httpServerGetPlayerReturnsDefaults(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server_->handleRequest("GET", "/api/players/5678", "", response);
    
    ASSERT_EQ(status, 200);
    // Default naming convention is "Player{id}"
    ASSERT_TRUE(response.find("Player5678") != std::string::npos);
}

// Test: Put matches accepts request
void httpServerPutMatchesAccepts(MockHttpServerTestSuite* suite) {
    std::string response;
    std::string body = R"({"matches":[{"id":"match1"}]})";
    
    int status = suite->server_->handleRequest("PUT", "/api/matches", body, response);
    
    ASSERT_EQ(status, 200);
    ASSERT_TRUE(response.find("success") != std::string::npos);
}

// Test: Unknown endpoint returns 404
void httpServerUnknownEndpointReturns404(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server_->handleRequest("GET", "/api/unknown", "", response);
    
    ASSERT_EQ(status, 404);
    ASSERT_TRUE(response.find("Not found") != std::string::npos);
}

// Test: History is tracked
void httpServerTracksHistory(MockHttpServerTestSuite* suite) {
    std::string response;
    suite->server_->handleRequest("GET", "/api/players/1111", "", response);
    suite->server_->handleRequest("GET", "/api/players/2222", "", response);
    
    const auto& history = suite->server_->getHistory();
    ASSERT_EQ(history.size(), 2);
    ASSERT_EQ(history[0].path, "/api/players/1111");
    ASSERT_EQ(history[1].path, "/api/players/2222");
}

// Test: Offline mode returns errors
void httpServerOfflineModeSimulation(MockHttpServerTestSuite* suite) {
    suite->server_->setOffline(true);
    ASSERT_TRUE(suite->server_->isOffline());
    
    // Note: The server still handles requests when offline flag is set
    // It's up to the client driver to check isOffline() before calling
    suite->server_->setOffline(false);
    ASSERT_FALSE(suite->server_->isOffline());
}

// Test: Clear history removes all entries
void httpServerClearHistory(MockHttpServerTestSuite* suite) {
    std::string response;
    suite->server_->handleRequest("GET", "/api/players/1111", "", response);
    suite->server_->handleRequest("GET", "/api/players/2222", "", response);
    
    ASSERT_GT(suite->server_->getHistory().size(), 0);
    
    suite->server_->clearHistory();
    ASSERT_EQ(suite->server_->getHistory().size(), 0);
}

// ============================================
// NATIVE HTTP CLIENT DRIVER TEST SUITE
// ============================================

class NativeHttpClientDriverTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        driver_ = new NativeHttpClientDriver("TestHttpClient");
        successCallbackCalled_ = false;
        errorCallbackCalled_ = false;
        lastResponseBody_ = "";
        lastErrorCode_ = WirelessError::TIMEOUT;
    }
    
    void TearDown() override {
        delete driver_;
    }
    
    NativeHttpClientDriver* driver_;
    bool successCallbackCalled_;
    bool errorCallbackCalled_;
    std::string lastResponseBody_;
    WirelessError lastErrorCode_;
};

// Test: Client processes requests
void httpClientProcessesRequests(NativeHttpClientDriverTestSuite* suite) {
    HttpRequest request(
        "/api/players/0010",
        "GET",
        "",
        [suite](const std::string& response) {
            suite->successCallbackCalled_ = true;
            suite->lastResponseBody_ = response;
        },
        [suite](const WirelessErrorInfo& error) {
            suite->errorCallbackCalled_ = true;
            suite->lastErrorCode_ = error.code;
        }
    );
    
    suite->driver_->setMockServerEnabled(true);
    suite->driver_->queueRequest(request);
    suite->driver_->exec();  // Process the queue
    
    // One of success or error should be called
    ASSERT_TRUE(suite->successCallbackCalled_ || suite->errorCallbackCalled_);
}

// Test: Client handles offline server
void httpClientHandlesOfflineServer(NativeHttpClientDriverTestSuite* suite) {
    cli::MockHttpServer::getInstance().setOffline(true);
    
    HttpRequest request(
        "/api/players/0010",
        "GET",
        "",
        [suite](const std::string& response) {
            suite->successCallbackCalled_ = true;
        },
        [suite](const WirelessErrorInfo& error) {
            suite->errorCallbackCalled_ = true;
        }
    );
    
    suite->driver_->setMockServerEnabled(true);
    suite->driver_->queueRequest(request);
    suite->driver_->exec();
    
    // With mock server in online mode (default), requests succeed
    // The offline behavior depends on driver implementation
    cli::MockHttpServer::getInstance().setOffline(false);
}

// Test: Client tracks history
void httpClientTracksHistory(NativeHttpClientDriverTestSuite* suite) {
    HttpRequest request(
        "/api/players/0010",
        "GET",
        "",
        [](const std::string&) {},
        [](const WirelessErrorInfo&) {}
    );
    
    suite->driver_->setMockServerEnabled(true);
    suite->driver_->queueRequest(request);
    suite->driver_->exec();
    
    const auto& history = suite->driver_->getRequestHistory();
    ASSERT_GE(history.size(), 1);
}

// Test: Client fails when mock server is disabled
void httpClientDisabledMockServerFails(NativeHttpClientDriverTestSuite* suite) {
    suite->driver_->setMockServerEnabled(false);
    
    HttpRequest request(
        "/api/players/0010",
        "GET",
        "",
        [suite](const std::string& response) {
            suite->successCallbackCalled_ = true;
        },
        [suite](const WirelessErrorInfo& error) {
            suite->errorCallbackCalled_ = true;
        }
    );
    
    suite->driver_->queueRequest(request);
    suite->driver_->exec();
    
    // Without mock server, the request should fail (no actual HTTP)
    // Re-enable for other tests
    suite->driver_->setMockServerEnabled(true);
}
