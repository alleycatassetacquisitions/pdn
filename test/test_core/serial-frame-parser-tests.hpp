#pragma once

#include <gtest/gtest.h>

#include "device/remote-device-coordinator.hpp"
#include "device/serial-frame-parser.hpp"
#include "utility-tests.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/crc16.hpp"

#include <cstdint>
#include <limits>
#include <vector>

class SerialFrameParserTests : public testing::Test {
public:
    /** Installs a fake clock so timeout behavior is deterministic. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
    }

    /** Restores the real platform clock. */
    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /**
     * Build a valid binary frame for a given opcode + payload; appends the CRC-16
     * computed over (opcode + payload). The 0xAA 0x55 preamble is prepended.
     * Built by hand rather than via encodeFramed so the tests cross-check the
     * framing codec and parser against an independent construction.
     */
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

    /** HELLO payload bytes: source mac[6] + deviceType[1] + headMac[6] + flags[1]. */
    std::vector<uint8_t> helloPayload(uint8_t firstMacByte = 0x10) {
        std::vector<uint8_t> p = {firstMacByte, 0x20, 0x30, 0x40, 0x50, 0x60,  // source
                                  0x01,                                        // deviceType
                                  0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,          // headMac
                                  0x00};                                       // flags
        EXPECT_EQ(p.size(), HELLO_PAYLOAD_LEN);
        return p;
    }

    /** Feeds the whole byte vector to the parser in one burst. */
    void feed(const std::vector<uint8_t>& bytes) {
        parser.feed(bytes.data(), bytes.size());
    }

    /** Application payload bytes counting up from `firstByte`. */
    std::vector<uint8_t> applicationPayload(uint8_t firstByte = 0x40) {
        std::vector<uint8_t> payload(APPLICATION_PAYLOAD_LEN);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<uint8_t>(firstByte + i);
        }
        return payload;
    }

    /**
     * Records every frame of either kind in arrival order: the first source
     * byte of each HELLO, the whole payload of each application frame.
     */
    void recordBothFrameKinds() {
        parser.setHelloFrameHandler(
        [this](const HelloPayload& hello) { helloSources.push_back(hello.source[0]); });
        parser.setApplicationFrameHandler([this](const ApplicationPayload& payload) {
            applicationPayloads.emplace_back(payload.begin(), payload.end());
        });
    }

    std::vector<uint8_t> helloSources;
    std::vector<std::vector<uint8_t>> applicationPayloads;

    SerialFrameParser parser;
    FakePlatformClock* fakeClock = nullptr;
};

TEST_F(SerialFrameParserTests, validFrameDecodesToHelloPayload) {
    int frameCount = 0;
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; ++frameCount; });

    feed(buildFrame(OP_HELLO, helloPayload()));

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.source[0], 0x10);
    EXPECT_EQ(got.source[5], 0x60);
    EXPECT_EQ(got.deviceType, 0x01);
    EXPECT_EQ(got.headMac[0], 0xAA);
    EXPECT_EQ(got.headMac[5], 0xFF);
    EXPECT_EQ(got.flags, 0x00);
}

TEST_F(SerialFrameParserTests, decodeMapsEveryWireByteToItsField) {
    // Independent of buildFrame/encodeFramed: a hand-laid frame whose CRC is the
    // precomputed CRC-16/XMODEM of (opcode + payload). Pins the exact byte->field
    // mapping so a reordered or repacked struct fails here, not silently on the wire.
    // Layout: preamble 0xAA 0x55, OP_HELLO 0x00, source[6]=11..66, deviceType=01,
    // headMac[6]=DE AD BE EF 00 01, flags=01, then CRC 0xDC48.
    const std::vector<uint8_t> frame = {0xAA, 0x55, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01,
                                        0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x01, 0xDC, 0x48};

    int frameCount = 0;
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; ++frameCount; });
    feed(frame);

    ASSERT_EQ(frameCount, 1);
    const uint8_t expectedSource[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint8_t expectedHead[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(got.source[i], expectedSource[i]);
        EXPECT_EQ(got.headMac[i], expectedHead[i]);
    }
    EXPECT_EQ(got.deviceType, 0x01);
    EXPECT_EQ(got.flags, 0x01);
}

TEST_F(SerialFrameParserTests, encodeFramedEmitsExactFrameBytes) {
    // The typed framing codec must produce the identical bytes the hand oracle above
    // expects: preamble + opcode + packed payload + precomputed CRC.
    HelloPayload hello{};
    const uint8_t source[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint8_t head[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    for (int i = 0; i < 6; ++i) {
        hello.source[i] = source[i];
        hello.headMac[i] = head[i];
    }
    hello.deviceType = 0x01;
    hello.flags = 0x01;

    // Same bytes the decode oracle above pins: preamble, OP_HELLO, payload, CRC 0xDC48.
    const std::vector<uint8_t> expected = {0xAA, 0x55, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01,
                                           0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x01, 0xDC, 0x48};
    EXPECT_EQ(encodeFramed(hello), expected);

    const std::array<uint8_t, HELLO_FRAME_LEN> fixedFrame = encodeHelloFrame(hello);
    EXPECT_EQ(std::vector<uint8_t>(fixedFrame.begin(), fixedFrame.end()), expected);
}

TEST_F(SerialFrameParserTests, encodeFramedRoundTripsThroughParser) {
    // The typed framing codec is the parser's inverse: a HelloPayload framed by
    // encodeFramed must decode back to the identical field values.
    int frameCount = 0;
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; ++frameCount; });

    HelloPayload sent{};
    sent.source[0] = 0xA7;
    sent.deviceType = 0x02;
    sent.headMac[3] = 0x99;
    sent.flags = 0x01;
    feed(encodeFramed(sent));

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.source[0], 0xA7);
    EXPECT_EQ(got.deviceType, 0x02);
    EXPECT_EQ(got.headMac[3], 0x99);
    EXPECT_EQ(got.flags, 0x01);
}

TEST_F(SerialFrameParserTests, badCrcDropsFrame) {
    int frameCount = 0;
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++frameCount; });

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
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++frameCount; });

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
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; ++frameCount; });

    std::vector<uint8_t> frame = buildFrame(OP_HELLO, helloPayload(0x01));
    for (uint8_t b : frame) {
        uint8_t single = b;
        parser.feed(&single, 1);
    }

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.source[0], 0x01);
    EXPECT_EQ(got.source[5], 0x60);
}

TEST_F(SerialFrameParserTests, midFrameTimeoutResets) {
    int frameCount = 0;
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++frameCount; });

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

TEST_F(SerialFrameParserTests, truncatedFrameCostsOnlyItsOwnFrame) {
    int frameCount = 0;
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; ++frameCount; });

    // The regime the wire actually produces: a frame cut short, then the next
    // HELLO one cadence later. 20ms never trips the 50ms mid-frame timeout, and
    // the driver drains the whole RX FIFO in one loop so the two frames can
    // arrive with no gap at all. The short frame therefore eats the start of the
    // good one, and only re-scanning the bytes it swallowed recovers the good
    // frame — otherwise one dropped byte costs two frames, not one.
    std::vector<uint8_t> truncated = buildFrame(OP_HELLO, helloPayload(0x01));
    truncated.resize(truncated.size() - 3);
    feed(truncated);

    fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
    feed(buildFrame(OP_HELLO, helloPayload(0xA2)));

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.source[0], 0xA2);
}

TEST_F(SerialFrameParserTests, droppedPayloadByteCostsOnlyItsOwnFrame) {
    int frameCount = 0;
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; ++frameCount; });

    // One byte lost mid-payload: the frame keeps its preamble but is a byte
    // short, so it reads two bytes of the next frame as its CRC. The bytes it
    // swallowed end in the next frame's 0xAA, so the replay must leave the
    // parser mid-preamble for the 0x55 that arrives after it.
    std::vector<uint8_t> shortByOne = buildFrame(OP_HELLO, helloPayload(0x01));
    shortByOne.erase(shortByOne.begin() + 8);
    feed(shortByOne);

    fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
    feed(buildFrame(OP_HELLO, helloPayload(0xA3)));

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.source[0], 0xA3);
}

TEST_F(SerialFrameParserTests, midFrameTimeoutFiresAcrossClockRollover) {
    int frameCount = 0;
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++frameCount; });

    std::vector<uint8_t> frame = buildFrame(OP_HELLO, helloPayload());
    fakeClock->setTime(std::numeric_limits<unsigned long>::max() - 10);
    parser.feed(frame.data(), 6);
    EXPECT_EQ(frameCount, 0);

    fakeClock->advance(60);
    parser.feed(frame.data() + 6, frame.size() - 6);
    EXPECT_EQ(frameCount, 0);

    feed(buildFrame(OP_HELLO, helloPayload(0xB7)));
    EXPECT_EQ(frameCount, 1);
}

TEST_F(SerialFrameParserTests, requestResetClearsPartialFrameAtNextFeed) {
    // The cross-thread reset used when a jack is declared dead: mid-frame state
    // from the dying connection must not corrupt the next device to plug in.
    int frameCount = 0;
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++frameCount; });

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
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; ++frameCount; });

    // Leading line noise with no preamble must be discarded, and the parser must
    // resync on the next 0xAA 0x55 rather than swallowing the following frame.
    feed({0x12, 0x34, 0xFF, 0x00, 0x55});
    feed(buildFrame(OP_HELLO, helloPayload(0xA1)));

    ASSERT_EQ(frameCount, 1);
    EXPECT_EQ(got.source[0], 0xA1);
}

TEST_F(SerialFrameParserTests, doubleAAResyncsToPreamble) {
    int frameCount = 0;
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++frameCount; });

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
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++frameCount; });

    // The 0xAA and 0x55 of the preamble arrive in separate feed() calls; the
    // GotAA state must survive across reads.
    std::vector<uint8_t> valid = buildFrame(OP_HELLO, helloPayload());
    feed({valid[0]});                                              // 0xAA
    feed(std::vector<uint8_t>(valid.begin() + 1, valid.end()));    // 0x55 + body

    EXPECT_EQ(frameCount, 1);
}

// ---- Application frame ----

// Preamble, OP_APPLICATION 0x02, payload 00..1F, then the precomputed
// CRC-16/XMODEM of (opcode + payload), 0xD966. Laid out by hand so the wire
// contract is pinned independently of the codec and of buildFrame.
inline std::vector<uint8_t> handLaidApplicationFrame() {
    std::vector<uint8_t> frame = {0xAA, 0x55, 0x02};
    for (uint8_t b = 0x00; b <= 0x1F; ++b) {
        frame.push_back(b);
    }
    frame.push_back(0xD9);
    frame.push_back(0x66);
    return frame;
}

TEST_F(SerialFrameParserTests, applicationFrameDecodesFromHandLaidBytes) {
    recordBothFrameKinds();

    const std::vector<uint8_t> frame = handLaidApplicationFrame();
    ASSERT_EQ(frame.size(), 37u);
    feed(frame);

    ASSERT_EQ(applicationPayloads.size(), 1u);
    EXPECT_EQ(applicationPayloads[0], applicationPayload(0x00));
    EXPECT_TRUE(helloSources.empty());
}

TEST_F(SerialFrameParserTests, encodeApplicationFrameEmitsExactFrameBytes) {
    ApplicationPayload payload{};
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }
    // Pre-filled so a byte the codec failed to write would show.
    ApplicationFrame frame;
    frame.fill(0xEE);
    encodeApplicationFrame(payload, frame);

    EXPECT_EQ(std::vector<uint8_t>(frame.begin(), frame.end()), handLaidApplicationFrame());
    EXPECT_EQ(buildFrame(OP_APPLICATION, applicationPayload(0x00)), handLaidApplicationFrame());
}

TEST_F(SerialFrameParserTests, applicationFrameRoundTripsThroughParser) {
    recordBothFrameKinds();

    // The payload is opaque: preamble bytes, zeros and 0xFF pass through as data.
    ApplicationPayload sent{};
    for (size_t i = 0; i < sent.size(); ++i) {
        sent[i] = static_cast<uint8_t>(0xF0 + i * 7);
    }
    sent[3] = 0xAA;
    sent[4] = 0x55;
    sent[5] = 0x00;
    sent[31] = 0xFF;
    ApplicationFrame frame{};
    encodeApplicationFrame(sent, frame);
    parser.feed(frame.data(), frame.size());

    ASSERT_EQ(applicationPayloads.size(), 1u);
    EXPECT_EQ(applicationPayloads[0], std::vector<uint8_t>(sent.begin(), sent.end()));
    EXPECT_TRUE(helloSources.empty());
}

TEST_F(SerialFrameParserTests, applicationFrameWithNoHandlerIsSkipped) {
    int helloCount = 0;
    parser.setHelloFrameHandler([&](const HelloPayload&) { ++helloCount; });

    feed(buildFrame(OP_APPLICATION, applicationPayload()));
    feed(buildFrame(OP_HELLO, helloPayload()));

    EXPECT_EQ(helloCount, 1);
}

TEST_F(SerialFrameParserTests, corruptedApplicationFrameIsDroppedAndTheNextHelloParses) {
    recordBothFrameKinds();

    // One payload bit flipped under an intact CRC.
    std::vector<uint8_t> damagedPayload = buildFrame(OP_APPLICATION, applicationPayload());
    damagedPayload[10] ^= 0x01;
    feed(damagedPayload);
    EXPECT_TRUE(applicationPayloads.empty());
    feed(buildFrame(OP_HELLO, helloPayload(0xA1)));
    EXPECT_EQ(helloSources, std::vector<uint8_t>({0xA1}));

    // An intact payload under a damaged CRC.
    std::vector<uint8_t> damagedCrc = buildFrame(OP_APPLICATION, applicationPayload());
    damagedCrc[damagedCrc.size() - 2] ^= 0xFF;
    damagedCrc[damagedCrc.size() - 1] ^= 0xFF;
    feed(damagedCrc);
    EXPECT_TRUE(applicationPayloads.empty());
    feed(buildFrame(OP_HELLO, helloPayload(0xA2)));
    EXPECT_EQ(helloSources, std::vector<uint8_t>({0xA1, 0xA2}));

    // And the parser still takes a good application frame afterwards.
    feed(buildFrame(OP_APPLICATION, applicationPayload(0x07)));
    ASSERT_EQ(applicationPayloads.size(), 1u);
    EXPECT_EQ(applicationPayloads[0], applicationPayload(0x07));
}

TEST_F(SerialFrameParserTests, applicationFrameOfTheWrongLengthIsDropped) {
    recordBothFrameKinds();

    // A sender that framed 31 or 33 bytes computed its CRC over them, but the
    // parser frames 32 from the opcode, so neither CRC lines up.
    std::vector<uint8_t> oneShort = applicationPayload();
    oneShort.pop_back();
    feed(buildFrame(OP_APPLICATION, oneShort));
    feed(buildFrame(OP_HELLO, helloPayload(0xA1)));

    std::vector<uint8_t> oneLong = applicationPayload();
    oneLong.push_back(0x77);
    feed(buildFrame(OP_APPLICATION, oneLong));
    feed(buildFrame(OP_HELLO, helloPayload(0xA2)));

    EXPECT_TRUE(applicationPayloads.empty());
    EXPECT_EQ(helloSources, std::vector<uint8_t>({0xA1, 0xA2}));
}

TEST_F(SerialFrameParserTests, truncatedApplicationFrameCostsNoHello) {
    recordBothFrameKinds();

    // An application frame cut short keeps reading its 32 payload bytes out of
    // the HELLO frames behind it: all of the first and the head of the second.
    // Replaying what it swallowed has to give both back, or one lost
    // application frame would cost two HELLO cadences of liveness.
    std::vector<uint8_t> truncated = buildFrame(OP_APPLICATION, applicationPayload());
    truncated.resize(10);
    feed(truncated);
    fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
    feed(buildFrame(OP_HELLO, helloPayload(0xA1)));
    fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
    feed(buildFrame(OP_HELLO, helloPayload(0xA2)));

    EXPECT_TRUE(applicationPayloads.empty());
    EXPECT_EQ(helloSources, std::vector<uint8_t>({0xA1, 0xA2}));
}

TEST_F(SerialFrameParserTests, damagedHelloInsideATruncatedApplicationFrameCostsOnlyItself) {
    recordBothFrameKinds();

    // The replay of the failed application frame holds a whole HELLO that fails
    // its own CRC, so the resync runs inside the resync. The frames after it
    // still have to come out in order.
    std::vector<uint8_t> truncated = buildFrame(OP_APPLICATION, applicationPayload());
    truncated.resize(10);
    std::vector<uint8_t> damagedHello = buildFrame(OP_HELLO, helloPayload(0xA1));
    damagedHello[8] ^= 0x10;
    feed(truncated);
    feed(damagedHello);
    feed(buildFrame(OP_HELLO, helloPayload(0xA2)));
    feed(buildFrame(OP_HELLO, helloPayload(0xA3)));

    EXPECT_TRUE(applicationPayloads.empty());
    EXPECT_EQ(helloSources, std::vector<uint8_t>({0xA2, 0xA3}));
}

TEST_F(SerialFrameParserTests, helloDamagedIntoTheApplicationOpcodeCostsOnlyItself) {
    recordBothFrameKinds();

    // A HELLO whose opcode byte arrives as OP_APPLICATION sends the parser
    // after 32 payload bytes it will never get: the rest of that HELLO and all
    // but the last byte of the next one. The next one must still be delivered.
    std::vector<uint8_t> damaged = buildFrame(OP_HELLO, helloPayload(0xA1));
    damaged[2] = OP_APPLICATION;
    feed(damaged);
    fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
    feed(buildFrame(OP_HELLO, helloPayload(0xA2)));
    fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
    feed(buildFrame(OP_HELLO, helloPayload(0xA3)));

    EXPECT_TRUE(applicationPayloads.empty());
    EXPECT_EQ(helloSources, std::vector<uint8_t>({0xA2, 0xA3}));
}

TEST_F(SerialFrameParserTests, stalledApplicationFrameTimesOutBeforeTheNextHello) {
    recordBothFrameKinds();

    std::vector<uint8_t> partial = buildFrame(OP_APPLICATION, applicationPayload());
    partial.resize(20);
    feed(partial);

    // Past the mid-frame timeout the half-read frame is gone, so the HELLO is
    // parsed as a frame and not read as the missing payload.
    fakeClock->advance(60);
    feed(buildFrame(OP_HELLO, helloPayload(0xA1)));

    EXPECT_TRUE(applicationPayloads.empty());
    EXPECT_EQ(helloSources, std::vector<uint8_t>({0xA1}));
}

TEST_F(SerialFrameParserTests, helloAndApplicationFramesInterleavedAtFrameBoundariesBothParse) {
    recordBothFrameKinds();

    std::vector<uint8_t> stream;
    const auto append = [&stream](const std::vector<uint8_t>& frame) {
        stream.insert(stream.end(), frame.begin(), frame.end());
    };
    append(buildFrame(OP_HELLO, helloPayload(0xA1)));
    append(buildFrame(OP_APPLICATION, applicationPayload(0x01)));
    append(buildFrame(OP_HELLO, helloPayload(0xA2)));
    append(buildFrame(OP_APPLICATION, applicationPayload(0x02)));
    append(buildFrame(OP_APPLICATION, applicationPayload(0x03)));
    append(buildFrame(OP_HELLO, helloPayload(0xA3)));

    const std::vector<uint8_t> expectedHellos = {0xA1, 0xA2, 0xA3};
    const std::vector<std::vector<uint8_t>> expectedPayloads = {
    applicationPayload(0x01), applicationPayload(0x02), applicationPayload(0x03)};

    // Once as a single burst.
    feed(stream);
    EXPECT_EQ(helloSources, expectedHellos);
    EXPECT_EQ(applicationPayloads, expectedPayloads);

    // And once a byte at a time, as the byte callback feeds it.
    helloSources.clear();
    applicationPayloads.clear();
    for (uint8_t b : stream) {
        parser.feed(&b, 1);
    }
    EXPECT_EQ(helloSources, expectedHellos);
    EXPECT_EQ(applicationPayloads, expectedPayloads);
}

TEST_F(SerialFrameParserTests, applicationPayloadHoldingAHelloFrameIsNotParsedAsHello) {
    recordBothFrameKinds();

    // A complete, CRC-valid HELLO frame sits inside the payload. It is payload:
    // only the application frame may come out.
    std::vector<uint8_t> payload = buildFrame(OP_HELLO, helloPayload(0xA1));
    payload.resize(APPLICATION_PAYLOAD_LEN, 0x5A);
    feed(buildFrame(OP_APPLICATION, payload));

    ASSERT_EQ(applicationPayloads.size(), 1u);
    EXPECT_EQ(applicationPayloads[0], payload);
    EXPECT_TRUE(helloSources.empty());
}
