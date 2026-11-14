#pragma once

#include <gtest/gtest.h>

#include "device-mock.hpp"
#include "rdc-test-harness.hpp"
#include "utility-tests.hpp"
#include "device/application-frame-hand-off.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/serial-frame-parser.hpp"
#include "protocol-constants.hpp"
#include "wireless/crc16.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

// ============================================
// RDC application frame on the serial jack
// ============================================
//
// Real RDCs cabled jack to jack through the shared harness: the test calls
// emitHello() where the ESP32 runs the rdc-hello task, so it is the HELLO
// writer and decides when a frame leaves.

using JackOutcome = std::pair<SerialIdentifier, ApplicationFrameOutcome>;

inline JackOutcome writtenOn(SerialIdentifier jack) {
    return {jack, ApplicationFrameOutcome::WRITTEN};
}

inline JackOutcome droppedOn(SerialIdentifier jack) {
    return {jack, ApplicationFrameOutcome::DROPPED};
}

// A cabled node that also records the application frames handed up to it, how
// the ones it sent ended, and how often a jack of its own disconnected.
struct ApplicationFrameNode : CabledRdcNode {
    /** Registers the recording observers on the node's RDC. */
    explicit ApplicationFrameNode(const uint8_t nodeMac[6])
        : CabledRdcNode(nodeMac) {
        rdc.setOnApplicationFrame(
        [this](SerialIdentifier jack, const ApplicationPayload& payload) {
            receivedJacks.push_back(jack);
            receivedPayloads.push_back(payload);
        },
        this);
        rdc.setOnApplicationFrameOutcome(
        [this](SerialIdentifier jack, ApplicationFrameOutcome outcome) {
            outcomes.push_back({jack, outcome});
        },
        this);
        rdc.setOnJackChange(
        [this](SerialIdentifier, bool connected) {
            if (!connected) disconnectCount++;
        },
        this);
    }

    std::vector<SerialIdentifier> receivedJacks;
    std::vector<ApplicationPayload> receivedPayloads;
    std::vector<JackOutcome> outcomes;
    int disconnectCount = 0;
};

// One whole frame as it sat on the wire.
struct WireFrame {
    uint8_t opcode = 0;
    std::vector<uint8_t> payload;
};

// Splits everything `jack` transmitted from byte `from` onward into whole
// frames. False when any byte falls outside a well-formed, CRC-valid frame,
// which is what one frame written into the middle of another would leave.
inline bool splitWholeFrames(const TappedSerialDriver& jack, size_t from,
                             std::vector<WireFrame>& frames) {
    const std::vector<uint8_t>& wire = jack.wire;
    size_t at = from;
    while (at < wire.size()) {
        if (wire.size() - at < 3) return false;
        if (wire[at] != FRAME_PREAMBLE_0 || wire[at + 1] != FRAME_PREAMBLE_1) return false;
        const uint8_t opcode = wire[at + 2];
        size_t payloadLength = 0;
        if (opcode == OP_HELLO) {
            payloadLength = HELLO_PAYLOAD_LEN;
        } else if (opcode == OP_APPLICATION) {
            payloadLength = APPLICATION_PAYLOAD_LEN;
        } else {
            return false;
        }
        const size_t crcAt = at + 3 + payloadLength;
        if (wire.size() < crcAt + 2) return false;
        const uint16_t crc = crc16(&wire[at + 2], 1 + payloadLength);
        if (wire[crcAt] != static_cast<uint8_t>(crc >> 8)) return false;
        if (wire[crcAt + 1] != static_cast<uint8_t>(crc & 0xFF)) return false;
        WireFrame frame;
        frame.opcode = opcode;
        frame.payload.assign(wire.begin() + static_cast<std::ptrdiff_t>(at + 3),
                             wire.begin() + static_cast<std::ptrdiff_t>(crcAt));
        frames.push_back(frame);
        at = crcAt + 2;
    }
    return true;
}

inline std::vector<uint8_t> opcodesOf(const std::vector<WireFrame>& frames) {
    std::vector<uint8_t> opcodes;
    for (const WireFrame& frame : frames) {
        opcodes.push_back(frame.opcode);
    }
    return opcodes;
}

// Counts up from `firstByte`, so two payloads are told apart.
inline ApplicationPayload testApplicationPayload(uint8_t firstByte) {
    ApplicationPayload payload{};
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(firstByte + i);
    }
    return payload;
}

inline std::vector<uint8_t> payloadBytes(const ApplicationPayload& payload) {
    return std::vector<uint8_t>(payload.begin(), payload.end());
}

class RdcApplicationFrameTests : public testing::Test {
public:
    /** Installs the fake clock the link machines and parsers read. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
    }

    /** Frees the nodes before the clock their timers read. */
    void TearDown() override {
        for (CabledRdcNode* node : nodes) {
            delete static_cast<ApplicationFrameNode*>(node);
        }
        nodes.clear();
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
        fakeClock = nullptr;
    }

    /** Adds a device with nothing plugged into its jacks. */
    ApplicationFrameNode* addNode(uint8_t lastMacByte) {
        const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, lastMacByte};
        auto* node = new ApplicationFrameNode(mac);
        nodes.push_back(node);
        return node;
    }

    /**
     * Cables `upper`'s OUTPUT jack to `lower`'s INPUT jack and runs both ends
     * to Connected.
     */
    void plug(ApplicationFrameNode* upper, ApplicationFrameNode* lower) {
        cables.push_back({indexOf(upper), indexOf(lower)});
        run(4);
        ASSERT_EQ(upper->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTED);
        ASSERT_EQ(lower->rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::CONNECTED);
    }

    /** Carries what has been written across the cables without another emit. */
    void deliver() { deliverCabled(nodes, cables); }

    /** Runs `cycles` HELLO cadences: emit, deliver, then one cadence of clock. */
    void run(int cycles) {
        for (int i = 0; i < cycles; ++i) {
            pumpCabledCycle(nodes, cables);
            fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        }
    }

    /**
     * `node` hears nothing on any jack for longer than the silent-link window,
     * then its main loop ticks: every link it held is gone.
     */
    void silenceLinksOf(ApplicationFrameNode* node) {
        fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
        node->rdc.sync(&node->device);
    }

    FakePlatformClock* fakeClock = nullptr;
    // Owned; every entry is an ApplicationFrameNode.
    std::vector<CabledRdcNode*> nodes;
    std::vector<std::pair<size_t, size_t>> cables;

private:
    size_t indexOf(const CabledRdcNode* node) const {
        return static_cast<size_t>(std::find(nodes.begin(), nodes.end(), node) - nodes.begin());
    }
};

inline void rdcApplicationFrameReachesThePeerOnThatJackOnly(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    ApplicationFrameNode* c = suite->addNode(0x0C);
    suite->plug(a, b);  // a.out -> b.in
    suite->plug(c, a);  // c.out -> a.in

    const ApplicationPayload payload = testApplicationPayload(0x10);
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, payload));
    EXPECT_TRUE(b->receivedPayloads.empty()) << "nothing leaves before the HELLO writer runs";
    EXPECT_TRUE(a->outcomes.empty());

    suite->run(1);

    ASSERT_EQ(b->receivedPayloads.size(), 1u);
    EXPECT_EQ(b->receivedJacks[0], SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(b->receivedPayloads[0], payload);
    EXPECT_TRUE(c->receivedPayloads.empty()) << "the frame also went out of the other jack";
    EXPECT_TRUE(a->receivedPayloads.empty());
    const std::vector<JackOutcome> writtenOnce = {writtenOn(SerialIdentifier::OUTPUT_JACK)};
    EXPECT_EQ(a->outcomes, writtenOnce);

    // Written once and reported once: later cadences carry HELLO alone.
    suite->run(3);
    EXPECT_EQ(b->receivedPayloads.size(), 1u);
    EXPECT_TRUE(c->receivedPayloads.empty());
    EXPECT_EQ(a->outcomes, writtenOnce);
    EXPECT_TRUE(b->outcomes.empty()) << "the receiver was told about a frame it never sent";
}

inline void
rdcApplicationFrameTravelsEitherWayAndEachJackQueuesItsOwn(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    ApplicationFrameNode* c = suite->addNode(0x0C);
    suite->plug(a, b);
    suite->plug(c, a);

    const ApplicationPayload upward = testApplicationPayload(0x20);
    ASSERT_TRUE(b->rdc.sendApplicationFrame(SerialIdentifier::INPUT_JACK, upward));
    suite->run(1);
    ASSERT_EQ(a->receivedPayloads.size(), 1u);
    EXPECT_EQ(a->receivedJacks[0], SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(a->receivedPayloads[0], upward);
    EXPECT_EQ(b->outcomes, std::vector<JackOutcome>({writtenOn(SerialIdentifier::INPUT_JACK)}));

    // One waiting on the OUTPUT jack does not stand in the way of the INPUT jack.
    const ApplicationPayload toOutputPeer = testApplicationPayload(0x30);
    const ApplicationPayload toInputPeer = testApplicationPayload(0x60);
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, toOutputPeer));
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::INPUT_JACK, toInputPeer));
    suite->run(1);

    ASSERT_EQ(b->receivedPayloads.size(), 1u);
    EXPECT_EQ(b->receivedJacks[0], SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(b->receivedPayloads[0], toOutputPeer);
    ASSERT_EQ(c->receivedPayloads.size(), 1u);
    EXPECT_EQ(c->receivedJacks[0], SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(c->receivedPayloads[0], toInputPeer);
    EXPECT_EQ(a->outcomes, std::vector<JackOutcome>({writtenOn(SerialIdentifier::OUTPUT_JACK),
                                                     writtenOn(SerialIdentifier::INPUT_JACK)}));
}

// The byte stream one device puts on a cable is whole frames end to end: each
// application frame sits between two HELLO frames, never inside one.
inline void rdcApplicationFrameNeverInterleavesWithHello(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    const size_t cabledFrom = a->out.wire.size();
    const size_t openFrom = a->in.wire.size();
    const std::vector<ApplicationPayload> sent = {
    testApplicationPayload(0x01), testApplicationPayload(0x41), testApplicationPayload(0x81)};
    for (int cadence = 0; cadence < 6; ++cadence) {
        if (cadence % 2 == 0) {
            ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK,
                                                    sent[static_cast<size_t>(cadence / 2)]));
        }
        suite->run(1);
    }

    std::vector<WireFrame> frames;
    ASSERT_TRUE(splitWholeFrames(a->out, cabledFrom, frames))
    << "a byte on the cable belongs to no whole frame";
    const std::vector<uint8_t> expectedOrder = {OP_HELLO, OP_APPLICATION, OP_HELLO,
                                                OP_HELLO, OP_APPLICATION, OP_HELLO,
                                                OP_HELLO, OP_APPLICATION, OP_HELLO};
    EXPECT_EQ(opcodesOf(frames), expectedOrder);
    ASSERT_EQ(frames.size(), expectedOrder.size());
    EXPECT_EQ(frames[1].payload, payloadBytes(sent[0]));
    EXPECT_EQ(frames[4].payload, payloadBytes(sent[1]));
    EXPECT_EQ(frames[7].payload, payloadBytes(sent[2]));

    // The same bytes through the parser the peer runs: nothing is lost to resync.
    SerialFrameParser parser;
    int helloCount = 0;
    std::vector<ApplicationPayload> parsed;
    parser.setHelloFrameHandler([&](const HelloPayload& hello) {
        if (memcmp(hello.source, a->mac, 6) == 0) ++helloCount;
    });
    parser.setApplicationFrameHandler(
    [&](const ApplicationPayload& payload) { parsed.push_back(payload); });
    parser.feed(a->out.wire.data() + cabledFrom, a->out.wire.size() - cabledFrom);
    EXPECT_EQ(helloCount, 6);
    EXPECT_EQ(parsed, sent);
    EXPECT_EQ(b->receivedPayloads, sent);

    // The jack nothing was sent on carried HELLO and only HELLO.
    std::vector<WireFrame> openJackFrames;
    ASSERT_TRUE(splitWholeFrames(a->in, openFrom, openJackFrames));
    EXPECT_EQ(opcodesOf(openJackFrames), std::vector<uint8_t>(6, OP_HELLO));
}

// On the ESP32 the main loop can queue a frame while the rdc-hello task is
// half-way through a HELLO. The HELLO is finished first.
inline void rdcApplicationFrameQueuedMidHelloFollowsThatHello(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    const ApplicationPayload payload = testApplicationPayload(0x10);
    const size_t from = a->out.wire.size();
    bool accepted = false;
    a->out.afterByte = [&](size_t count) {
        if (count != from + 5) return;
        accepted = a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, payload);
    };
    a->rdc.emitHello();
    a->out.afterByte = nullptr;

    ASSERT_TRUE(accepted);
    std::vector<WireFrame> frames;
    ASSERT_TRUE(splitWholeFrames(a->out, from, frames)) << "the frame cut into the HELLO";
    EXPECT_EQ(opcodesOf(frames), std::vector<uint8_t>({OP_HELLO, OP_APPLICATION}));
}

// The other half of that race: the main loop runs while the writer is part-way
// through the frame. The frame is the writer's until it is out, so a send is
// refused, nothing is reported yet, and the frame on the wire is intact.
inline void rdcMainLoopDuringAWriteLeavesTheFrameIntact(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    const ApplicationPayload first = testApplicationPayload(0x10);
    const ApplicationPayload second = testApplicationPayload(0x90);
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, first));

    const size_t from = a->out.wire.size();
    bool acceptedMidWrite = true;
    size_t outcomesMidWrite = 99;
    a->out.afterByte = [&](size_t count) {
        // Ten bytes into the application frame, which follows the HELLO.
        if (count != from + HELLO_FRAME_LEN + 10) return;
        acceptedMidWrite = a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, second);
        a->rdc.sync(&a->device);
        outcomesMidWrite = a->outcomes.size();
    };
    a->rdc.emitHello();
    a->out.afterByte = nullptr;

    EXPECT_FALSE(acceptedMidWrite);
    EXPECT_EQ(outcomesMidWrite, 0u) << "reported written with bytes still to go";
    std::vector<WireFrame> frames;
    ASSERT_TRUE(splitWholeFrames(a->out, from, frames));
    ASSERT_EQ(opcodesOf(frames), std::vector<uint8_t>({OP_HELLO, OP_APPLICATION}));
    EXPECT_EQ(frames[1].payload, payloadBytes(first));

    // The refused frame was not kept for later.
    suite->deliver();
    suite->run(2);
    ASSERT_EQ(b->receivedPayloads.size(), 1u);
    EXPECT_EQ(b->receivedPayloads[0], first);
    EXPECT_EQ(a->outcomes, std::vector<JackOutcome>({writtenOn(SerialIdentifier::OUTPUT_JACK)}));
}

// A peer that disconnects while the writer is part-way through the frame does
// not get it cut short or taken back. The frame is past recall, so it is
// reported written at the disconnect, once, and not again when it is finished.
inline void
rdcFrameBegunBeforeADisconnectIsFinishedAndReportedWrittenOnce(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    const ApplicationPayload payload = testApplicationPayload(0x10);
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, payload));

    const size_t from = a->out.wire.size();
    std::vector<JackOutcome> outcomesAtDisconnect;
    a->out.afterByte = [&](size_t count) {
        if (count != from + HELLO_FRAME_LEN + 10) return;
        suite->silenceLinksOf(a);
        outcomesAtDisconnect = a->outcomes;
    };
    a->rdc.emitHello();
    a->out.afterByte = nullptr;

    const std::vector<JackOutcome> writtenOnce = {writtenOn(SerialIdentifier::OUTPUT_JACK)};
    ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::DISCONNECTED);
    ASSERT_EQ(a->disconnectCount, 1);
    EXPECT_EQ(outcomesAtDisconnect, writtenOnce);
    std::vector<WireFrame> frames;
    ASSERT_TRUE(splitWholeFrames(a->out, from, frames)) << "the frame was cut short";
    ASSERT_EQ(opcodesOf(frames), std::vector<uint8_t>({OP_HELLO, OP_APPLICATION}));
    EXPECT_EQ(frames[1].payload, payloadBytes(payload));

    // The cable is still in, so the frame arrives and the link comes back. The
    // peer that returns hears nothing more about the old frame, and the jack
    // is free for a new one.
    suite->deliver();
    suite->run(6);
    ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTED);
    ASSERT_EQ(b->receivedPayloads.size(), 1u);
    EXPECT_EQ(b->receivedPayloads[0], payload);
    EXPECT_EQ(a->outcomes, writtenOnce);
    EXPECT_TRUE(
    a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, testApplicationPayload(0x90)));
}

// At most one frame per jack is in hand: a second send is refused, not queued
// behind the first and not written over it, until the first has been reported.
inline void
rdcSecondApplicationFrameIsRefusedUntilTheFirstIsReported(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    const ApplicationPayload first = testApplicationPayload(0x10);
    const ApplicationPayload second = testApplicationPayload(0x90);
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, first));
    EXPECT_FALSE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, second));

    // Written, but the main loop has not ticked to report it.
    a->rdc.emitHello();
    EXPECT_FALSE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, second));
    EXPECT_TRUE(a->outcomes.empty());

    suite->deliver();
    suite->run(1);
    ASSERT_EQ(b->receivedPayloads.size(), 1u) << "a refused frame was written as well";
    EXPECT_EQ(b->receivedPayloads[0], first) << "a refused frame replaced the waiting one";
    ASSERT_EQ(a->outcomes, std::vector<JackOutcome>({writtenOn(SerialIdentifier::OUTPUT_JACK)}));

    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, second));
    suite->run(1);
    ASSERT_EQ(b->receivedPayloads.size(), 2u);
    EXPECT_EQ(b->receivedPayloads[1], second);
    EXPECT_EQ(a->outcomes.size(), 2u);
}

// A frame still waiting when its peer disconnects is never written, to that
// peer or to whoever is on the jack next, and the sender is told so once.
inline void rdcApplicationFrameWaitingAtDisconnectIsDropped(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    const ApplicationPayload undelivered = testApplicationPayload(0x10);
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, undelivered));
    const size_t from = a->out.wire.size();

    suite->silenceLinksOf(a);

    const std::vector<JackOutcome> droppedOnce = {droppedOn(SerialIdentifier::OUTPUT_JACK)};
    ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::DISCONNECTED);
    ASSERT_EQ(a->disconnectCount, 1);
    EXPECT_EQ(a->outcomes, droppedOnce);

    // The cable is still in, so the link comes back. The dropped frame does
    // not, and nothing about it is reported under the new link.
    suite->run(6);
    ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTED);
    EXPECT_TRUE(b->receivedPayloads.empty());
    std::vector<WireFrame> frames;
    ASSERT_TRUE(splitWholeFrames(a->out, from, frames));
    EXPECT_EQ(opcodesOf(frames), std::vector<uint8_t>(frames.size(), OP_HELLO));
    EXPECT_EQ(a->outcomes, droppedOnce);

    // The jack takes a new frame for the new link, and that one is what arrives.
    const ApplicationPayload next = testApplicationPayload(0x90);
    ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, next));
    suite->run(1);
    ASSERT_EQ(b->receivedPayloads.size(), 1u);
    EXPECT_EQ(b->receivedPayloads[0], next);
    EXPECT_EQ(a->outcomes, std::vector<JackOutcome>({droppedOn(SerialIdentifier::OUTPUT_JACK),
                                                     writtenOn(SerialIdentifier::OUTPUT_JACK)}));
}

// A jack-change observer runs while the dying jack still reads Connected, so a
// send from inside it is accepted. That frame is normally reported DROPPED; on
// a device it may be written and reported WRITTEN, because the hello task can
// take it before the link-down drop. Here the test is the writer and does not.
inline void
rdcApplicationFrameSentFromTheDisconnectObserverIsDropped(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    const ApplicationPayload payload = testApplicationPayload(0x10);
    int observer = 0;
    bool acceptedInObserver = false;
    a->rdc.setOnJackChange(
    [&](SerialIdentifier jack, bool connected) {
        if (connected) return;
        acceptedInObserver = a->rdc.sendApplicationFrame(jack, payload);
    },
    &observer);
    const size_t from = a->out.wire.size();

    suite->silenceLinksOf(a);

    ASSERT_TRUE(acceptedInObserver);
    EXPECT_EQ(a->outcomes, std::vector<JackOutcome>({droppedOn(SerialIdentifier::OUTPUT_JACK)}));
    a->rdc.emitHello();
    std::vector<WireFrame> frames;
    ASSERT_TRUE(splitWholeFrames(a->out, from, frames));
    EXPECT_EQ(opcodesOf(frames), std::vector<uint8_t>({OP_HELLO}));
}

// An outcome is reported once. A frame already reported written draws no
// second report when its peer leaves afterwards.
inline void
rdcReportedApplicationFrameIsNotReportedAgainAtDisconnect(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    ASSERT_TRUE(
    a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, testApplicationPayload(0x10)));
    suite->run(1);
    const std::vector<JackOutcome> writtenOnce = {writtenOn(SerialIdentifier::OUTPUT_JACK)};
    ASSERT_EQ(a->outcomes, writtenOnce);

    suite->silenceLinksOf(a);

    ASSERT_EQ(a->disconnectCount, 1);
    EXPECT_EQ(a->outcomes, writtenOnce);
}

// A link can end between main-loop ticks, when a different peer's HELLO is
// heard on the jack. A frame written since the last tick is reported there,
// before the new peer's link opens, and not again on the next tick.
inline void rdcWrittenFrameIsReportedBeforeAPeerSwapOpensANewLink(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    ASSERT_TRUE(
    a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, testApplicationPayload(0x10)));
    a->rdc.emitHello();
    ASSERT_TRUE(a->outcomes.empty());

    const uint8_t stranger[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x5E};
    deliverFrame(a->out, chainHelloFrame(stranger, nullptr));

    const std::vector<JackOutcome> writtenOnce = {writtenOn(SerialIdentifier::OUTPUT_JACK)};
    ASSERT_EQ(a->disconnectCount, 1);
    EXPECT_EQ(a->outcomes, writtenOnce);
    ASSERT_NE(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTED);

    a->rdc.sync(&a->device);
    a->rdc.sync(&a->device);
    EXPECT_EQ(a->outcomes, writtenOnce);
}

// A frame is for the peer Connected on the jack. With no such peer there is
// nobody to write it to, so nothing is queued and nothing reported.
inline void rdcApplicationFrameIsRefusedWithoutAConnectedPeer(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    const ApplicationPayload payload = testApplicationPayload(0x10);

    ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::DISCONNECTED);
    EXPECT_FALSE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, payload));

    // A peer is heard but its context exchange has not finished.
    b->rdc.emitHello();
    pumpCable(b->in, a->out);
    a->rdc.sync(&a->device);
    ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTING);
    EXPECT_FALSE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, payload));

    const size_t from = a->out.wire.size();
    a->rdc.emitHello();
    a->rdc.sync(&a->device);
    std::vector<WireFrame> frames;
    ASSERT_TRUE(splitWholeFrames(a->out, from, frames));
    EXPECT_EQ(opcodesOf(frames), std::vector<uint8_t>({OP_HELLO}));
    EXPECT_TRUE(a->outcomes.empty());
}

// An application frame takes the wire for about as long as a HELLO cadence.
// Each step holds the sender's next HELLO back by the promised bound (one
// cadence plus both frames' wire time) and ticks both main loops before it
// arrives.
inline void rdcLinkStaysConnectedAcrossApplicationFrames(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    // 8N1: ten bits on the wire for each byte.
    const unsigned long bothFramesWireMs =
    (HELLO_FRAME_LEN + APPLICATION_FRAME_LEN) * 10 * 1000 / BAUDRATE;
    const unsigned long worstHelloGapMs =
    RemoteDeviceCoordinator::HELLO_CADENCE_MS + bothFramesWireMs;
    ASSERT_LT(worstHelloGapMs, RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS);

    std::vector<ApplicationPayload> sent;
    for (uint8_t step = 0; step < 5; ++step) {
        sent.push_back(testApplicationPayload(static_cast<uint8_t>(step * 0x20)));
        ASSERT_TRUE(a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, sent.back()));
        a->rdc.emitHello();
        b->rdc.emitHello();
        suite->fakeClock->advance(worstHelloGapMs);
        a->rdc.sync(&a->device);
        b->rdc.sync(&b->device);
        ASSERT_EQ(b->rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::CONNECTED);
        ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTED);
        suite->deliver();
    }

    EXPECT_EQ(a->disconnectCount, 0);
    EXPECT_EQ(b->disconnectCount, 0);
    EXPECT_EQ(b->receivedPayloads, sent);
    EXPECT_EQ(a->outcomes, std::vector<JackOutcome>(5, writtenOn(SerialIdentifier::OUTPUT_JACK)));
}

// Observers are kept per owner, so a second one takes nothing away from the
// first, and removing one leaves the other.
inline void rdcApplicationFrameObserversAreKeptPerOwner(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    int secondOwner = 0;
    int secondFrameCount = 0;
    int secondOutcomeCount = 0;
    b->rdc.setOnApplicationFrame(
    [&](SerialIdentifier, const ApplicationPayload&) { ++secondFrameCount; }, &secondOwner);
    a->rdc.setOnApplicationFrameOutcome(
    [&](SerialIdentifier, ApplicationFrameOutcome) { ++secondOutcomeCount; }, &secondOwner);

    ASSERT_TRUE(
    a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, testApplicationPayload(0x10)));
    suite->run(1);
    EXPECT_EQ(b->receivedPayloads.size(), 1u);
    EXPECT_EQ(secondFrameCount, 1);
    EXPECT_EQ(a->outcomes.size(), 1u);
    EXPECT_EQ(secondOutcomeCount, 1);

    b->rdc.setOnApplicationFrame(nullptr, &secondOwner);
    a->rdc.setOnApplicationFrameOutcome(nullptr, &secondOwner);
    ASSERT_TRUE(
    a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, testApplicationPayload(0x50)));
    suite->run(1);
    EXPECT_EQ(b->receivedPayloads.size(), 2u);
    EXPECT_EQ(secondFrameCount, 1);
    EXPECT_EQ(a->outcomes.size(), 2u);
    EXPECT_EQ(secondOutcomeCount, 1);
}

// The coordinator's copy of a payload must not outlive the coordinator, and
// nobody is told how a frame ended that way. The node is built in storage the
// test keeps, so what it leaves behind can be read after it is destroyed.
inline void rdcDestructionClearsAWaitingFrameAndReportsNothing(RdcApplicationFrameTests* suite) {
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0A};
    void* storage = ::operator new(sizeof(ApplicationFrameNode));
    auto* a = new (storage) ApplicationFrameNode(mac);
    suite->nodes.push_back(a);
    ApplicationFrameNode* b = suite->addNode(0x0B);
    suite->plug(a, b);

    int reportCount = 0;
    a->rdc.setOnApplicationFrameOutcome(
    [&](SerialIdentifier, ApplicationFrameOutcome) { ++reportCount; }, &reportCount);
    const ApplicationPayload payload = testApplicationPayload(0x10);
    const bool accepted = a->rdc.sendApplicationFrame(SerialIdentifier::OUTPUT_JACK, payload);
    const uint8_t* first = static_cast<const uint8_t*>(storage);
    const uint8_t* last = first + sizeof(ApplicationFrameNode);
    const bool heldBefore = std::search(first, last, payload.begin(), payload.end()) != last;

    // Out of the fixture's hands, then destroyed here, storage kept.
    suite->nodes.erase(suite->nodes.begin());
    suite->cables.clear();
    a->~ApplicationFrameNode();
    const bool heldAfter = std::search(first, last, payload.begin(), payload.end()) != last;
    ::operator delete(storage);

    EXPECT_TRUE(accepted);
    EXPECT_TRUE(heldBefore) << "the check below would pass on a slot that was never filled";
    EXPECT_FALSE(heldAfter) << "the payload outlived the coordinator";
    EXPECT_EQ(reportCount, 0) << "destruction reported an outcome";
}

// The coordinator checks a frame and hands it up; whether its sender is to be
// believed is the consumer's call, so a frame is handed up on a jack with no
// link at all. A damaged one is not handed up anywhere.
inline void rdcApplicationFrameIsHandedUpWhateverTheLinkState(RdcApplicationFrameTests* suite) {
    ApplicationFrameNode* a = suite->addNode(0x0A);
    ASSERT_EQ(a->rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::DISCONNECTED);

    const ApplicationPayload payload = testApplicationPayload(0x10);
    ApplicationFrame frame{};
    encodeApplicationFrame(payload, frame);
    std::vector<uint8_t> damaged(frame.begin(), frame.end());
    damaged[12] ^= 0x04;
    deliverFrame(a->in, damaged);
    EXPECT_TRUE(a->receivedPayloads.empty());

    deliverFrame(a->in, std::vector<uint8_t>(frame.begin(), frame.end()));
    ASSERT_EQ(a->receivedPayloads.size(), 1u);
    EXPECT_EQ(a->receivedJacks[0], SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(a->receivedPayloads[0], payload);
    EXPECT_EQ(a->rdc.getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::DISCONNECTED)
    << "an application frame is not a HELLO and opens no link";
}
