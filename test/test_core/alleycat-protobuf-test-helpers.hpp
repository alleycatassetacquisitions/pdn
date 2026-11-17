#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
#include <vector>

#include "alleycat-server/device_api.pb.h"
#include "device-mock.hpp"
#include "device/wireless-manager.hpp"

// Server-side encoding helpers for building protobuf response fixtures.

inline std::string encodeTestMessage(const pb_msgdesc_t* fields, const void* message) {
    std::vector<pb_byte_t> buffer(16384);
    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
    EXPECT_TRUE(pb_encode(&stream, fields, message)) << PB_GET_ERROR(&stream);
    return std::string(reinterpret_cast<const char*>(buffer.data()), stream.bytes_written);
}

/**
 * Appends a length-delimited field to an encoded message. Lets fixtures carry
 * values the static nanopb structs cannot hold, such as an oversized string or
 * one repeated entry past max_count.
 */
inline void appendTestBytesField(std::string& payload, uint32_t tag, const std::string& value) {
    std::vector<pb_byte_t> buffer(value.size() + 16);
    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
    EXPECT_TRUE(pb_encode_tag(&stream, PB_WT_STRING, tag) &&
                pb_encode_string(&stream, reinterpret_cast<const pb_byte_t*>(value.data()), value.size()))
        << PB_GET_ERROR(&stream);
    payload.append(reinterpret_cast<const char*>(buffer.data()), stream.bytes_written);
}

/** Captures every request queued through a real WirelessManager. */
class AlleycatApiTestSuite : public testing::Test {
public:
    /** Wires WirelessManager to capture queued HTTP requests. */
    void SetUp() override {
        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        ON_CALL(httpClient, queueRequest(testing::_))
            .WillByDefault(testing::Invoke([this](HttpRequest&& request) {
                queuedRequests.push_back(std::move(request));
                return true;
            }));
    }

    /** Releases WirelessManager owned in SetUp. */
    void TearDown() override {
        delete wirelessManager;
        wirelessManager = nullptr;
    }

    testing::NiceMock<MockHttpClient> httpClient;
    testing::NiceMock<MockPeerComms> peerComms;
    WirelessManager* wirelessManager = nullptr;
    std::vector<HttpRequest> queuedRequests;
};
