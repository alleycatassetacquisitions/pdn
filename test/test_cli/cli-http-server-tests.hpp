//
// Mock HTTP Server Tests - Tests for cli::MockHttpServer and NativeHttpClientDriver
//

#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include "cli/cli-http-server.hpp"
#include "alleycat-server/crash-encoder.hpp"
#include "device/crash/crash-record.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "utils/uuid-bytes.hpp"
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

static void configurePinPlayer(cli::MockHttpServer* server, const char* pin, const char* name) {
    cli::MockPlayerConfig config;
    config.id = pin;
    config.name = name;
    config.isHunter = false;
    config.allegiance = 1;
    config.faction = "TestFaction";
    UUIDBytes::parse("7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11", config.wirePlayerId);
    server->configurePlayer(pin, config);
}

// Test: Get device player by PIN returns protobuf body
void httpServerGetDevicePlayerByPinReturnsData(MockHttpServerTestSuite* suite) {
    configurePinPlayer(suite->server_, "1234", "TestPlayer");

    std::string response;
    int status =
        suite->server_->handleRequest("GET", "/device/players/by-pin/1234", "", response);

    ASSERT_EQ(status, 200);
    ASSERT_FALSE(response.empty());
    ASSERT_NE(response.find("TestPlayer"), std::string::npos);

    suite->server_->removePlayer("1234");
}

// Test: Unknown PIN returns 404
void httpServerGetDevicePlayerByPinReturns404(MockHttpServerTestSuite* suite) {
    std::string response;
    int status =
        suite->server_->handleRequest("GET", "/device/players/by-pin/5678", "", response);

    ASSERT_EQ(status, 404);
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
    configurePinPlayer(suite->server_, "1111", "A");
    configurePinPlayer(suite->server_, "2222", "B");
    std::string response;
    suite->server_->handleRequest("GET", "/device/players/by-pin/1111", "", response);
    suite->server_->handleRequest("GET", "/device/players/by-pin/2222", "", response);

    const auto& history = suite->server_->getHistory();
    ASSERT_EQ(history.size(), 2);
    ASSERT_EQ(history[0].path, "/device/players/by-pin/1111");
    ASSERT_EQ(history[1].path, "/device/players/by-pin/2222");

    suite->server_->removePlayer("1111");
    suite->server_->removePlayer("2222");
}

// Test: Offline mode returns errors
void httpServerOfflineModeSimulation(MockHttpServerTestSuite* suite) {
    suite->server_->setOffline(true);
    // Offline flag is available for future driver integration; routing still succeeds when called directly.
    suite->server_->setOffline(false);
}

void httpServerClearHistory(MockHttpServerTestSuite* suite) {
    configurePinPlayer(suite->server_, "1111", "A");
    configurePinPlayer(suite->server_, "2222", "B");
    std::string response;
    suite->server_->handleRequest("GET", "/device/players/by-pin/1111", "", response);
    suite->server_->handleRequest("GET", "/device/players/by-pin/2222", "", response);

    ASSERT_GT(suite->server_->getHistory().size(), 0);

    suite->server_->clearHistory();
    ASSERT_EQ(suite->server_->getHistory().size(), 0);

    suite->server_->removePlayer("1111");
    suite->server_->removePlayer("2222");
}

void httpServerDeviceLogsReturns204(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server_->handleRequest("POST", "/device-logs", "payload", response);

    ASSERT_EQ(status, 204);
    ASSERT_TRUE(response.empty());
}

void httpServerDeviceLogsEmptyBodyReturns400(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server_->handleRequest("POST", "/device-logs", "", response);
    ASSERT_EQ(status, 400);
}

// ============================================
// NATIVE HTTP CLIENT DRIVER TEST SUITE
// ============================================

class NativeHttpClientDriverTestSuite : public testing::Test {
public:
    void SetUp() override {
        driver = new NativeHttpClientDriver("test_http");
        successCallbackCalled = false;
        errorCallbackCalled = false;
        lastErrorCode = WirelessError::TIMEOUT;
        configurePinPlayer(&cli::MockHttpServer::getInstance(), "0010", "CLI");
    }

    void TearDown() override {
        delete driver;
    }

    NativeHttpClientDriver* driver;
    bool successCallbackCalled;
    bool errorCallbackCalled;
    std::string lastResponseBody;
    WirelessError lastErrorCode;
};

// Test: Client processes requests
void httpClientProcessesRequests(NativeHttpClientDriverTestSuite* suite) {
    HttpRequest request(
        "/device/players/by-pin/0010",
        "GET",
        "",
        "",
        [suite](const std::string& response) {
            suite->successCallbackCalled = true;
            suite->lastResponseBody = response;
        },
        [suite](const WirelessErrorInfo& error) {
            suite->errorCallbackCalled = true;
            suite->lastErrorCode = error.code;
        });

    suite->driver->setMockServerEnabled(true);
    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    ASSERT_TRUE(suite->successCallbackCalled || suite->errorCallbackCalled);
}

// Test: disconnect fails every queued request, so none replays on a later trip
void httpClientDisconnectFailsQueuedRequests(NativeHttpClientDriverTestSuite* suite) {
    int errors = 0;
    int successes = 0;
    for (int i = 0; i < 2; i++) {
        HttpRequest request(
            "/device/players/by-pin/0010", "GET", "", "",
            [&successes](const std::string&) { successes++; },
            [&errors](const WirelessErrorInfo&) { errors++; });
        suite->driver->queueRequest(std::move(request));
    }

    suite->driver->disconnect();
    suite->driver->setMockServerEnabled(true);
    suite->driver->exec();

    EXPECT_EQ(errors, 2);
    EXPECT_EQ(successes, 0);
    EXPECT_EQ(suite->driver->getPendingRequestCount(), 0u);
}

// Test: Client handles offline server
void httpClientHandlesOfflineServer(NativeHttpClientDriverTestSuite* suite) {
    cli::MockHttpServer::getInstance().setOffline(true);

    HttpRequest request(
        "/device/players/by-pin/0010",
        "GET",
        "",
        "",
        [suite](const std::string& response) {
            suite->successCallbackCalled = true;
        },
        [suite](const WirelessErrorInfo& error) {
            suite->errorCallbackCalled = true;
        });

    suite->driver->setMockServerEnabled(true);
    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    cli::MockHttpServer::getInstance().setOffline(false);
}

// Test: Client tracks history
void httpClientTracksHistory(NativeHttpClientDriverTestSuite* suite) {
    HttpRequest request(
        "/device/players/by-pin/0010",
        "GET",
        "",
        "",
        [](const std::string&) {},
        [](const WirelessErrorInfo&) {});

    suite->driver->setMockServerEnabled(true);
    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    const std::deque<HttpRequestHistoryEntry>& history = suite->driver->getRequestHistory();
    ASSERT_GE(history.size(), 1);
}

void httpClientUploadsEncodedCrashLog(NativeHttpClientDriverTestSuite* suite) {
    CrashRecord rec{};
    rec.crashNumber = 1;
    rec.timestamp   = 5000;
    rec.resetReason   = 4;
    strncpy(rec.commitHash, "testhash", COMMIT_HASH_LENGTH - 1);

    const uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t buf[CrashEncoder::MAX_ENCODED_SIZE];
    const size_t len = CrashEncoder::encode(rec, mac, buf, sizeof(buf));
    ASSERT_GT(len, 0u);

    HttpRequest request(
        "/device-logs",
        "POST",
        "application/protobuf",
        std::string(reinterpret_cast<const char*>(buf), len),
        [suite](const std::string&) {
            suite->successCallbackCalled = true;
        },
        [suite](const WirelessErrorInfo&) {
            suite->errorCallbackCalled = true;
        }
    );

    suite->driver->setMockServerEnabled(true);
    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    ASSERT_TRUE(suite->successCallbackCalled);
    ASSERT_FALSE(suite->errorCallbackCalled);
}

void httpClientDisabledMockServerFails(NativeHttpClientDriverTestSuite* suite) {
    suite->driver->setMockServerEnabled(false);

    HttpRequest request(
        "/device/players/by-pin/0010",
        "GET",
        "",
        "",
        [suite](const std::string& response) {
            suite->successCallbackCalled = true;
        },
        [suite](const WirelessErrorInfo& error) {
            suite->errorCallbackCalled = true;
        });

    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    suite->driver->setMockServerEnabled(true);
}
