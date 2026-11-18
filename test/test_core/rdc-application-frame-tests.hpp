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

// What one emit pass put on a jack: the opcode of each frame in order, and the
// application frames' kinds and bytes.
struct DecodedJackOutput {
    std::vector<uint8_t> opcodes;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> applicationFrames;
};

inline DecodedJackOutput decodeJackOutput(NativeSerialDriver& jack) {
    DecodedJackOutput decoded;
    SerialFrameParser parser;
    parser.setHelloFrameHandler([&](const HelloPayload&) { decoded.opcodes.push_back(OP_HELLO); });
    parser.setApplicationFrameHandler([&](uint8_t kind, const uint8_t* payload, size_t length) {
        decoded.opcodes.push_back(OP_APPLICATION);
        decoded.applicationFrames.emplace_back(kind,
                                               std::vector<uint8_t>(payload, payload + length));
    });
    const std::string out = jack.getOutput();
    jack.clearOutput();
    const std::vector<uint8_t> bytes(out.begin(), out.end());
    parser.feed(bytes.data(), bytes.size());
    return decoded;
}

using ApplicationFrameOutcomes = std::vector<std::pair<SerialIdentifier, ApplicationFrameOutcome>>;

// Each kind reaches only its own receiver, with the jack it came in on; a kind
// nobody registered reaches nobody; a second receiver for a kind is refused
// rather than taking the kind over, and the kind is free once removed.
inline void rdcApplicationFrameRoutesByKindWithItsJack(RDCHelloTests* suite) {
    using Received = std::vector<std::pair<SerialIdentifier, std::vector<uint8_t>>>;
    Received gotA;
    Received gotB;
    auto recordInto = [](Received& log) {
        return [&log](SerialIdentifier jack, const uint8_t* payload, size_t length) {
            log.emplace_back(jack, std::vector<uint8_t>(payload, payload + length));
        };
    };
    int intruderCount = 0;
    auto intruder = [&](SerialIdentifier, const uint8_t*, size_t) { ++intruderCount; };
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(TEST_KIND_A, recordInto(gotA)));
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(TEST_KIND_B, recordInto(gotB)));
    EXPECT_FALSE(suite->rdc.setOnApplicationFrame(TEST_KIND_A, intruder));

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK, suite->helloFrame(0xB1));
    deliverFrame(suite->inJack, applicationFrameBytes(TEST_KIND_B, {1, 2, 3}));
    deliverFrame(suite->inJack, applicationFrameBytes(TEST_KIND_UNREGISTERED, {4}));
    deliverFrame(suite->inJack, applicationFrameBytes(TEST_KIND_A, {5, 6}));

    EXPECT_EQ(gotB, (Received{{SerialIdentifier::INPUT_JACK, {1, 2, 3}}}));
    EXPECT_EQ(gotA, (Received{{SerialIdentifier::INPUT_JACK, {5, 6}}}));
    EXPECT_EQ(intruderCount, 0);

    EXPECT_TRUE(suite->rdc.setOnApplicationFrame(TEST_KIND_A, nullptr));
    EXPECT_TRUE(suite->rdc.setOnApplicationFrame(TEST_KIND_A, intruder));
    deliverFrame(suite->inJack, applicationFrameBytes(TEST_KIND_A, {7}));
    EXPECT_EQ(intruderCount, 1);
    EXPECT_EQ(gotA.size(), 1u);
}

// A frame counts only from a jack holding a HELLO peer: not before the first
// HELLO, and not once the peer has gone silent.
inline void rdcApplicationFrameWithoutAHelloPeerIsDropped(RDCHelloTests* suite) {
    int receivedCount = 0;
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(
    TEST_KIND_A, [&](SerialIdentifier, const uint8_t*, size_t) { ++receivedCount; }));

    deliverFrame(suite->outJack, applicationFrameBytes(TEST_KIND_A, {1}));
    EXPECT_EQ(receivedCount, 0);

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    deliverFrame(suite->outJack, applicationFrameBytes(TEST_KIND_A, {1}));
    EXPECT_EQ(receivedCount, 1);

    suite->fakeClock->advance(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    deliverFrame(suite->outJack, applicationFrameBytes(TEST_KIND_A, {1}));
    EXPECT_EQ(receivedCount, 1);
}

// A frame is accepted only for a Connected peer and only within a frame's
// capacity, goes out straight after that jack's HELLO and on no other jack,
// and holds the jack's one slot until the sync() that reports it WRITTEN.
inline void rdcApplicationFrameIsWrittenAfterHelloToAConnectedJack(RDCHelloTests* suite) {
    ApplicationFrameOutcomes outcomes;
    ASSERT_TRUE(suite->rdc.setOnApplicationFrame(
    TEST_KIND_A, nullptr, [&](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        outcomes.emplace_back(jack, outcome);
    }));
    const std::vector<uint8_t> bytes = {1, 2, 3, 4};
    auto send = [&](size_t length) {
        return suite->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, TEST_KIND_A,
                                               bytes.data(), length);
    };

    EXPECT_FALSE(send(bytes.size())) << "no peer";
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    EXPECT_FALSE(send(bytes.size())) << "Connecting is not Connected";
    suite->rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);

    const std::vector<uint8_t> oversize(APPLICATION_PAYLOAD_MAX_LEN + 1, 0x5A);
    EXPECT_FALSE(suite->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, TEST_KIND_A,
                                                 oversize.data(), oversize.size()));
    ASSERT_TRUE(send(bytes.size()));
    EXPECT_FALSE(send(bytes.size())) << "one frame queued per jack";

    suite->outJack.clearOutput();
    suite->inJack.clearOutput();
    suite->rdc.emitHello();
    const DecodedJackOutput out = decodeJackOutput(suite->outJack);
    EXPECT_EQ(out.opcodes, (std::vector<uint8_t>{OP_HELLO, OP_APPLICATION}));
    ASSERT_EQ(out.applicationFrames.size(), 1u);
    EXPECT_EQ(out.applicationFrames[0].first, TEST_KIND_A);
    EXPECT_EQ(out.applicationFrames[0].second, bytes);
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
    TEST_KIND_A, nullptr, [&](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
        outcomes.emplace_back(jack, outcome);
    }));
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xA1));
    const uint8_t bytes[] = {1, 2, 3};
    ASSERT_TRUE(suite->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, TEST_KIND_A, bytes,
                                                sizeof(bytes)));

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(outcomes, (ApplicationFrameOutcomes{
                        {SerialIdentifier::OUTPUT_JACK, ApplicationFrameOutcome::DROPPED}}));

    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(decodeJackOutput(suite->outJack).opcodes, std::vector<uint8_t>{OP_HELLO});
}
