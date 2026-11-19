#pragma once

#include <gtest/gtest.h>

#include "application-frame-hand-off-tests.hpp"
#include "rdc-hello-tests.hpp"

#include <cstring>
#include <utility>
#include <vector>

// ============================================
// Application frame through the coordinator
// ============================================

constexpr ApplicationFrameKind KIND_A = ApplicationFrameKind::RECORD_KEY;
constexpr ApplicationFrameKind KIND_B = ApplicationFrameKind::MATCH_SECRET;

/** The MAC RDCHelloTests::helloFrame(firstByte) advertises. */
inline std::vector<uint8_t> helloSource(uint8_t firstByte) {
    return {firstByte, 0x02, 0x03, 0x04, 0x05, 0x06};
}

/** A frame of `kind` as the HELLO peer helloFrame(firstByte) would send it. */
inline std::vector<uint8_t> peerFrame(uint8_t firstByte, ApplicationFrameKind kind,
                                      const std::vector<uint8_t>& bytes) {
    return applicationFrameBytes(static_cast<uint8_t>(kind), helloSource(firstByte).data(), bytes);
}

using ReceivedFrames = std::vector<std::pair<SerialIdentifier, std::vector<uint8_t>>>;

inline RemoteDeviceCoordinator::ApplicationFrameCallback recordInto(ReceivedFrames& log) {
    return [&log](SerialIdentifier jack, const uint8_t* payload, size_t length) {
        log.emplace_back(jack, std::vector<uint8_t>(payload, payload + length));
    };
}

// What one emit pass put on a jack: the opcode of each frame in order, and the
// application frames' headers and bytes.
struct DecodedJackOutput {
    std::vector<uint8_t> opcodes;
    std::vector<ApplicationFrameHeader> headers;
    std::vector<std::vector<uint8_t>> payloads;
};

inline DecodedJackOutput decodeJackOutput(NativeSerialDriver& jack) {
    DecodedJackOutput decoded;
    SerialFrameParser parser;
    parser.setHelloFrameHandler([&](const HelloPayload&) { decoded.opcodes.push_back(OP_HELLO); });
    parser.setApplicationFrameHandler(
    [&](const ApplicationFrameHeader& header, const uint8_t* payload) {
        decoded.opcodes.push_back(OP_APPLICATION);
        decoded.headers.push_back(header);
        decoded.payloads.emplace_back(payload, payload + header.length);
    });
    const std::string out = jack.getOutput();
    jack.clearOutput();
    const std::vector<uint8_t> bytes(out.begin(), out.end());
    parser.feed(bytes.data(), bytes.size());
    return decoded;
}

using ApplicationFrameOutcomes = std::vector<std::pair<SerialIdentifier, ApplicationFrameOutcome>>;

// Each kind reaches only its own receiver, with the jack it came in on; a kind
// outside the registry reaches nobody; a second owner for a kind is refused
// rather than taking the kind over, and the kind is free once removed.
inline void rdcApplicationFrameRoutesByKindWithItsJack(RDCHelloTests* suite) {
    ReceivedFrames gotA;
    ReceivedFrames gotB;
    int intruderCount = 0;
    auto intruder = [&](SerialIdentifier, const uint8_t*, size_t) { ++intruderCount; };
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(KIND_A, recordInto(gotA)));
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(KIND_B, recordInto(gotB)));
    EXPECT_FALSE(suite->rdc.setOnApplicationFrame(KIND_A, intruder));

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK, suite->helloFrame(0xB1));
    deliverFrame(suite->inJack, peerFrame(0xB1, KIND_B, {1, 2, 3}));
    deliverFrame(suite->inJack, applicationFrameBytes(0x7F, helloSource(0xB1).data(), {4}));
    deliverFrame(suite->inJack, peerFrame(0xB1, KIND_A, {5, 6}));

    EXPECT_EQ(gotB, (ReceivedFrames{{SerialIdentifier::INPUT_JACK, {1, 2, 3}}}));
    EXPECT_EQ(gotA, (ReceivedFrames{{SerialIdentifier::INPUT_JACK, {5, 6}}}));
    EXPECT_EQ(intruderCount, 0);

    EXPECT_TRUE(suite->rdc.setOnApplicationFrame(KIND_A, nullptr));
    EXPECT_TRUE(suite->rdc.setOnApplicationFrame(KIND_A, intruder));
    deliverFrame(suite->inJack, peerFrame(0xB1, KIND_A, {7}));
    EXPECT_EQ(intruderCount, 1);
    EXPECT_EQ(gotA.size(), 1u);
}

// Only the jack's HELLO peer is heard: not before any HELLO, not this
// device's own frame echoed back, and not a stranger's.
inline void rdcApplicationFrameCountsOnlyFromTheJacksHelloPeer(RDCHelloTests* suite) {
    ReceivedFrames got;
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(KIND_A, recordInto(got)));

    deliverFrame(suite->outJack, peerFrame(0xA1, KIND_A, {1}));
    EXPECT_TRUE(got.empty()) << "before any HELLO";

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xA1));
    deliverFrame(suite->outJack,
                 applicationFrameBytes(static_cast<uint8_t>(KIND_A), suite->localMac, {2}));
    deliverFrame(suite->outJack, peerFrame(0xC1, KIND_A, {3}));
    EXPECT_TRUE(got.empty()) << "own echo or a stranger";

    deliverFrame(suite->outJack, peerFrame(0xA1, KIND_A, {4}));
    EXPECT_EQ(got, (ReceivedFrames{{SerialIdentifier::OUTPUT_JACK, {4}}}));
}

// A frame from a peer still Connecting is held, latest wins, and handed up
// once on the Connected edge, with the jack already Connected; one held when
// the peer leaves is wiped and never handed up.
inline void rdcApplicationFrameFromAConnectingPeerWaitsForItToConnect(RDCHelloTests* suite) {
    ReceivedFrames got;
    JackStatus statusWhenReceived = JackStatus::DISCONNECTED;
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(
    KIND_A, [&](SerialIdentifier jack, const uint8_t* payload, size_t length) {
        statusWhenReceived = suite->rdc.getJackStatus(jack);
        got.emplace_back(jack, std::vector<uint8_t>(payload, payload + length));
    }));

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    deliverFrame(suite->outJack, peerFrame(0xA1, KIND_A, {1}));
    deliverFrame(suite->outJack, peerFrame(0xA1, KIND_A, {2}));
    EXPECT_TRUE(got.empty());

    suite->rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(got, (ReceivedFrames{{SerialIdentifier::OUTPUT_JACK, {2}}}));
    EXPECT_EQ(statusWhenReceived, JackStatus::CONNECTED);

    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);
    deliverFrame(suite->inJack, peerFrame(0xB1, KIND_A, {3}));
    suite->fakeClock->advance(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK, suite->helloFrame(0xB1));
    EXPECT_EQ(got.size(), 1u) << "the frame held when the peer left is gone";
}

// A frame is accepted only for a Connected peer and only within a frame's
// capacity, goes out from this device straight after that jack's HELLO and on
// no other jack, and holds the jack's one slot until the sync() that reports
// it WRITTEN.
inline void rdcApplicationFrameIsWrittenAfterHelloToAConnectedJack(RDCHelloTests* suite) {
    ApplicationFrameOutcomes outcomes;
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(
    KIND_A, nullptr, [&](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        outcomes.emplace_back(jack, outcome);
    }));
    const std::vector<uint8_t> bytes = {1, 2, 3, 4};
    auto send = [&](size_t length) {
        return suite->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, KIND_A, bytes.data(),
                                               length);
    };

    EXPECT_FALSE(send(bytes.size())) << "no peer";
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    EXPECT_FALSE(send(bytes.size())) << "Connecting is not Connected";
    suite->rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);

    const std::vector<uint8_t> oversize(APPLICATION_PAYLOAD_MAX_LEN + 1, 0x5A);
    EXPECT_FALSE(suite->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, KIND_A,
                                                 oversize.data(), oversize.size()));
    ASSERT_TRUE(send(bytes.size()));
    EXPECT_FALSE(send(bytes.size())) << "one frame queued per jack";

    suite->outJack.clearOutput();
    suite->inJack.clearOutput();
    suite->rdc.emitHello();
    const DecodedJackOutput out = decodeJackOutput(suite->outJack);
    EXPECT_EQ(out.opcodes, (std::vector<uint8_t>{OP_HELLO, OP_APPLICATION}));
    ASSERT_EQ(out.headers.size(), 1u);
    EXPECT_EQ(out.headers[0].kind, static_cast<uint8_t>(KIND_A));
    EXPECT_EQ(0, memcmp(out.headers[0].source, suite->localMac, 6));
    EXPECT_EQ(out.payloads[0], bytes);
    EXPECT_EQ(decodeJackOutput(suite->inJack).opcodes, std::vector<uint8_t>{OP_HELLO});

    EXPECT_TRUE(outcomes.empty());
    EXPECT_FALSE(send(bytes.size())) << "outcome not yet reported";
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(outcomes, (ApplicationFrameOutcomes{
                        {SerialIdentifier::OUTPUT_JACK, ApplicationFrameOutcome::WRITTEN}}));
    EXPECT_TRUE(send(bytes.size()));
}

// A frame still waiting when its peer goes is reported DROPPED and never written.
inline void rdcApplicationFrameIsDroppedWhenThePeerLeavesFirst(RDCHelloTests* suite) {
    ApplicationFrameOutcomes outcomes;
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(
    KIND_A, nullptr, [&](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        outcomes.emplace_back(jack, outcome);
    }));
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xA1));
    const uint8_t bytes[] = {1, 2, 3};
    ASSERT_TRUE(
    suite->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, KIND_A, bytes, sizeof(bytes)));

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(outcomes, (ApplicationFrameOutcomes{
                        {SerialIdentifier::OUTPUT_JACK, ApplicationFrameOutcome::DROPPED}}));

    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(decodeJackOutput(suite->outJack).opcodes, std::vector<uint8_t>{OP_HELLO});
}
