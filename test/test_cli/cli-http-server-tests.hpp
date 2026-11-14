//
// Mock HTTP Server Tests - Tests for cli::MockHttpServer and NativeHttpClientDriver
//

#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include "cli/cli-http-server.hpp"
#include "alleycat-server/crash-codec.hpp"
#include "device/crash/crash-record.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "utils/uuid-bytes.hpp"
#include "wireless/wireless-types.hpp"

// ============================================
// MOCK HTTP SERVER TEST SUITE
// ============================================

class MockHttpServerTestSuite : public testing::Test {
public:  // Public for test function access
    /** Every test talks to the one process-wide mock server. */
    void SetUp() override { server = &cli::MockHttpServer::getInstance(); }

    cli::MockHttpServer* server;
};

static void configurePinPlayer(cli::MockHttpServer* server, const char* pin, const char* name) {
    cli::MockPlayerConfig config;
    config.name = name;
    UUIDBytes::parse("7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11", config.wirePlayerId);
    server->configurePlayer(pin, config);
}

// Test: Get device player by PIN returns protobuf body
void httpServerGetDevicePlayerByPinReturnsData(MockHttpServerTestSuite* suite) {
    configurePinPlayer(suite->server, "1234", "TestPlayer");

    std::string response;
    int status = suite->server->handleRequest("GET", "/device/players/by-pin/1234", "", response);

    ASSERT_EQ(status, 200);
    ASSERT_FALSE(response.empty());
    ASSERT_NE(response.find("TestPlayer"), std::string::npos);

    suite->server->removePlayer("1234");
}

// Test: Unknown PIN returns 404
void httpServerGetDevicePlayerByPinReturns404(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server->handleRequest("GET", "/device/players/by-pin/5678", "", response);

    ASSERT_EQ(status, 404);
}

// Test: Unknown endpoint returns 404
void httpServerUnknownEndpointReturns404(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server->handleRequest("GET", "/api/unknown", "", response);

    ASSERT_EQ(status, 404);
    ASSERT_TRUE(response.find("Not found") != std::string::npos);
}

void httpServerDeviceLogsReturns204(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server->handleRequest("POST", "/device-logs", "payload", response);

    ASSERT_EQ(status, 204);
    ASSERT_TRUE(response.empty());
}

void httpServerDeviceLogsEmptyBodyReturns400(MockHttpServerTestSuite* suite) {
    std::string response;
    int status = suite->server->handleRequest("POST", "/device-logs", "", response);
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
        cli::MockHttpServer::getInstance().setOffline(false);
    }

    NativeHttpClientDriver* driver;
    bool successCallbackCalled;
    bool errorCallbackCalled;
    std::string lastResponseBody;
    WirelessError lastErrorCode;
    int lastHttpStatus = -1;
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

    suite->driver->setHttpClientState(HttpClientState::CONNECTED);
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
    suite->driver->setHttpClientState(HttpClientState::CONNECTED);
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
            suite->lastErrorCode = error.code;
        });

    suite->driver->setHttpClientState(HttpClientState::CONNECTED);
    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    EXPECT_TRUE(suite->errorCallbackCalled);
    EXPECT_FALSE(suite->successCallbackCalled);
    EXPECT_EQ(suite->lastErrorCode, WirelessError::WIFI_NOT_CONNECTED);
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

    suite->driver->setHttpClientState(HttpClientState::CONNECTED);
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
    FdnStoredCrashEntry entry{};
    memcpy(entry.senderMac, mac, sizeof(entry.senderMac));
    entry.kind = static_cast<uint8_t>(RecordKind::CRASH);
    entry.recordNumber = rec.crashNumber;
    const CrashPacket packet = toCrashPacket(rec);
    memcpy(entry.content.recordBytes, &packet, sizeof(packet));
    uint8_t buf[CrashCodec::MAX_ENCODED_SIZE];
    const size_t len = CrashCodec::encode(entry, buf, sizeof(buf));
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

    suite->driver->setHttpClientState(HttpClientState::CONNECTED);
    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    ASSERT_TRUE(suite->successCallbackCalled);
    ASSERT_FALSE(suite->errorCallbackCalled);
}

// Test: a request queued with no WiFi association fails without reaching the server
void httpClientDisconnectedFails(NativeHttpClientDriverTestSuite* suite) {
    ASSERT_FALSE(suite->driver->isConnected());

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
            suite->lastErrorCode = error.code;
        });

    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();

    ASSERT_FALSE(suite->successCallbackCalled);
    ASSERT_TRUE(suite->errorCallbackCalled);
    ASSERT_EQ(suite->lastErrorCode, WirelessError::WIFI_NOT_CONNECTED);
    ASSERT_TRUE(suite->driver->getRequestHistory().empty());
}

// Test: a reply body over MAX_HTTP_RESPONSE_BYTES fails as INVALID_RESPONSE, never reaching
// onSuccess
void httpClientRejectsOversizedResponse(NativeHttpClientDriverTestSuite* suite) {
    // The encoded body carries the whole name, so a name past the bound makes the body exceed it.
    const std::string longName(MAX_HTTP_RESPONSE_BYTES + 1, 'x');
    configurePinPlayer(&cli::MockHttpServer::getInstance(), "0011", longName.c_str());

    HttpRequest request(
    "/device/players/by-pin/0011", "GET", "", "",
    [suite](const std::string&) { suite->successCallbackCalled = true; },
    [suite](const WirelessErrorInfo& error) {
        suite->errorCallbackCalled = true;
        suite->lastErrorCode = error.code;
    });

    suite->driver->setHttpClientState(HttpClientState::CONNECTED);
    suite->driver->queueRequest(std::move(request));
    suite->driver->exec();
    cli::MockHttpServer::getInstance().removePlayer("0011");

    EXPECT_FALSE(suite->successCallbackCalled);
    EXPECT_TRUE(suite->errorCallbackCalled);
    EXPECT_EQ(suite->lastErrorCode, WirelessError::INVALID_RESPONSE);
}

// Test: an HTTP error response reaches the caller with its status, and a failure with no
// response carries none
void httpClientReportsTheStatusOfAnErrorResponse(NativeHttpClientDriverTestSuite* suite) {
    suite->driver->setHttpClientState(HttpClientState::CONNECTED);
    // The mock server answers an empty device log with 400.
    for (const bool serverOffline : {false, true}) {
        cli::MockHttpServer::getInstance().setOffline(serverOffline);
        suite->errorCallbackCalled = false;
        suite->lastHttpStatus = -1;
        HttpRequest request(
        "/device-logs", "POST", "application/protobuf", "",
        [suite](const std::string&) { suite->successCallbackCalled = true; },
        [suite](const WirelessErrorInfo& error) {
            suite->errorCallbackCalled = true;
            suite->lastHttpStatus = error.httpStatus;
        });
        suite->driver->queueRequest(std::move(request));
        suite->driver->exec();

        EXPECT_FALSE(suite->successCallbackCalled);
        ASSERT_TRUE(suite->errorCallbackCalled);
        EXPECT_EQ(suite->lastHttpStatus, serverOffline ? 0 : 400);
    }
}
