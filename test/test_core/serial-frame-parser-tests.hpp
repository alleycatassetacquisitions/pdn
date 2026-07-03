#pragma once

#include <gtest/gtest.h>

#include "device/serial-frame-parser.hpp"
#include "utility-tests.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/crc16.hpp"

#include <cstdint>
#include <vector>

class SerialFrameParserTests : public testing::Test {
public:
    /// Installs a fake clock so timeout behavior is deterministic.
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
    }

    /// Restores the real platform clock.
    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /// Build a valid binary frame for a given opcode + payload; appends the CRC-16
    /// computed over (opcode + payload). The 0xAA 0x55 preamble is prepended.
    /// Built by hand rather than via encodeFramed so the tests cross-check the
    /// encoder and parser against an independent construction.
    std::vector<uint8_t> buildFrame(uint8_t opcode, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> crcInput;
        crcInput.push_back(opcode);
        crcInput.insert(crcInput.end(), payload.begin(), payload.end());
        const uint16_t c = crc16(crcInput.data(), crcInput.size());

        std::vector<uint8_t> out;
        out.push_back(0xAA);
        out.push_back(0x55);
        out.push_back(opcode);
        out.insert(out.end(), payload.begin(), payload.end());
        out.push_back(static_cast<uint8_t>((c >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(c & 0xFF));
        return out;
    }

    /// HELLO payload: source mac[6] + deviceType[1] + headMac[6] + confirmed[1].
    std::vector<uint8_t> helloPayload(uint8_t firstMacByte = 0x10) {
        std::vector<uint8_t> p = {firstMacByte, 0x20, 0x30, 0x40, 0x50, 0x60,  // source
                                  0x01,                                        // deviceType
                                  0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,          // headMac
                                  0x00};                                       // confirmed
        EXPECT_EQ(p.size(), HELLO_PAYLOAD_LEN);
        return p;
    }

    /// Feeds the whole byte vector to the parser in one burst.
    void feed(const std::vector<uint8_t>& bytes) {
        parser.feed(bytes.data(), bytes.size());
    }

    SerialFrameParser parser;
    FakePlatformClock* fakeClock = nullptr;
};

TEST_F(SerialFrameParserTests, validBinaryFrameRoutesToFrameHandler) {
    int frameCount = 0;
    Frame got;
    parser.setBinaryFrameHandler([&](const Frame& f) { got = f; ++frameCount; });

    std::vector<uint8_t> frame = buildFrame(OP_HELLO, helloPayload());
    feed(frame);

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.opcode, OP_HELLO);
    ASSERT_EQ(got.payload.size(), HELLO_PAYLOAD_LEN);
    EXPECT_EQ(got.payload[0], 0x10);
    EXPECT_EQ(got.payload[13], 0x00);
}

TEST_F(SerialFrameParserTests, encodeFramedRoundTripsThroughParser) {
    // The encoder is the parser's inverse: a frame produced by encodeFramed
    // must parse back to the identical opcode + payload.
    int frameCount = 0;
    Frame got;
    parser.setBinaryFrameHandler([&](const Frame& f) { got = f; ++frameCount; });

    std::vector<uint8_t> payload = helloPayload(0xA7);
    std::vector<uint8_t> frame = encodeFramed(OP_HELLO, payload);
    feed(frame);

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.opcode, OP_HELLO);
    EXPECT_EQ(got.payload, payload);
}

TEST_F(SerialFrameParserTests, badCrcDropsFrame) {
    int frameCount = 0;
    parser.setBinaryFrameHandler([&](const Frame&) { ++frameCount; });

    // Full-length frame so the parser consumes everything and actually reaches
    // the CRC comparison. A short payload would end mid-CRC-read and drop the
    // frame regardless of whether CRC checking works, making this a vacuous
    // test of the rejection path.
    std::vector<uint8_t> frame = buildFrame(OP_HELLO, helloPayload());
    frame[frame.size() - 2] ^= 0xFF;
    frame[frame.size() - 1] ^= 0xFF;
    feed(frame);

    EXPECT_EQ(frameCount, 0);

    // The parser must have resynced, not swallowed the next frame: a valid frame
    // immediately after the bad one still parses.
    feed(buildFrame(OP_HELLO, helloPayload()));
    EXPECT_EQ(frameCount, 1);
}

TEST_F(SerialFrameParserTests, unknownOpcodeDrops) {
    int frameCount = 0;
    parser.setBinaryFrameHandler([&](const Frame&) { ++frameCount; });

    // 0x99 is not an opcode; neither is 0x01 (the fork's BEACON, not ported).
    feed({0xAA, 0x55, 0x99, 0x01, 0x02, 0x03, 0x04});
    feed({0xAA, 0x55, 0x01, 0x01, 0x02, 0x03, 0x04});
    EXPECT_EQ(frameCount, 0);

    // After resync, a valid frame should still parse cleanly.
    feed(buildFrame(OP_HELLO, helloPayload()));
    EXPECT_EQ(frameCount, 1);
}

TEST_F(SerialFrameParserTests, fragmentedFrameAssembles) {
    int frameCount = 0;
    Frame got;
    parser.setBinaryFrameHandler([&](const Frame& f) { got = f; ++frameCount; });

    std::vector<uint8_t> frame = buildFrame(OP_HELLO, helloPayload(0x01));
    for (uint8_t b : frame) {
        uint8_t single = b;
        parser.feed(&single, 1);
    }

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.opcode, OP_HELLO);
    ASSERT_EQ(got.payload.size(), HELLO_PAYLOAD_LEN);
    EXPECT_EQ(got.payload[0], 0x01);
    EXPECT_EQ(got.payload[5], 0x60);
}

TEST_F(SerialFrameParserTests, midFrameTimeoutResets) {
    int frameCount = 0;
    parser.setBinaryFrameHandler([&](const Frame&) { ++frameCount; });

    // Inject just the preamble + opcode + partial payload, then stall.
    feed({0xAA, 0x55, OP_HELLO, 0xDE, 0xAD, 0xBE});
    EXPECT_EQ(frameCount, 0);

    // Advance the clock past the 50ms timeout. The parser checks the mid-frame
    // timeout at the start of the next feed(), so the partial frame is cleared
    // before the fresh frame below is consumed.
    fakeClock->advance(60);

    feed(buildFrame(OP_HELLO, helloPayload()));
    EXPECT_EQ(frameCount, 1);
}

TEST_F(SerialFrameParserTests, requestResetClearsPartialFrameAtNextFeed) {
    // The cross-thread reset used when a jack is declared dead: mid-frame state
    // from the dying connection must not corrupt the next device to plug in.
    int frameCount = 0;
    parser.setBinaryFrameHandler([&](const Frame&) { ++frameCount; });

    // Partial frame in flight, then a reset requested from "another thread".
    feed({0xAA, 0x55, OP_HELLO, 0x01, 0x02, 0x03});
    parser.requestReset();

    // Without the reset these bytes would land mid-payload of the stale frame;
    // with it, the fresh frame parses cleanly at the next feed.
    feed(buildFrame(OP_HELLO, helloPayload()));
    EXPECT_EQ(frameCount, 1);
}

TEST_F(SerialFrameParserTests, garbageThenValidFrameResyncs) {
    int frameCount = 0;
    Frame got;
    parser.setBinaryFrameHandler([&](const Frame& f) { got = f; ++frameCount; });

    // Leading line noise with no preamble must be discarded, and the parser must
    // resync on the next 0xAA 0x55 rather than swallowing the following frame.
    feed({0x12, 0x34, 0xFF, 0x00, 0x55});
    feed(buildFrame(OP_HELLO, helloPayload(0xA1)));

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.opcode, OP_HELLO);
    EXPECT_EQ(got.payload[0], 0xA1);
}

TEST_F(SerialFrameParserTests, doubleAAResyncsToPreamble) {
    int frameCount = 0;
    parser.setBinaryFrameHandler([&](const Frame&) { ++frameCount; });

    // A stray extra 0xAA before the real preamble: GotAA on another 0xAA must
    // stay in GotAA so 0xAA 0xAA 0x55 still opens a frame rather than aborting.
    std::vector<uint8_t> valid = buildFrame(OP_HELLO, helloPayload());
    std::vector<uint8_t> withExtraAA;
    withExtraAA.push_back(0xAA);
    withExtraAA.insert(withExtraAA.end(), valid.begin(), valid.end());
    feed(withExtraAA);

    EXPECT_EQ(frameCount, 1);
}

TEST_F(SerialFrameParserTests, splitPreambleAcrossFeeds) {
    int frameCount = 0;
    parser.setBinaryFrameHandler([&](const Frame&) { ++frameCount; });

    // The 0xAA and 0x55 of the preamble arrive in separate feed() calls; the
    // GotAA state must survive across reads.
    std::vector<uint8_t> valid = buildFrame(OP_HELLO, helloPayload());
    feed({valid[0]});                                              // 0xAA
    feed(std::vector<uint8_t>(valid.begin() + 1, valid.end()));    // 0x55 + body

    EXPECT_EQ(frameCount, 1);
}
