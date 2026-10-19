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

inline bool encodeTestString(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const std::string* value = static_cast<const std::string*>(*arg);
    return pb_encode_tag_for_field(stream, field) &&
           pb_encode_string(stream, reinterpret_cast<const pb_byte_t*>(value->data()), value->size());
}

inline bool encodeTestStrings(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const std::vector<std::string>* values = static_cast<const std::vector<std::string>*>(*arg);
    for (const std::string& value : *values) {
        void* const valueArg = const_cast<std::string*>(&value);
        if (!encodeTestString(stream, field, &valueArg)) {
            return false;
        }
    }
    return true;
}

inline void bindTestString(pb_callback_t& callback, const std::string& value) {
    callback.funcs.encode = &encodeTestString;
    callback.arg = const_cast<std::string*>(&value);
}

inline std::string encodeTestMessage(const pb_msgdesc_t* fields, const void* message) {
    std::vector<pb_byte_t> buffer(4096);
    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
    EXPECT_TRUE(pb_encode(&stream, fields, message)) << PB_GET_ERROR(&stream);
    return std::string(reinterpret_cast<const char*>(buffer.data()), stream.bytes_written);
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
