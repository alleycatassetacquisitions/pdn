#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include <vector>

#include "game/node-network/node-network.hpp"
#include "game/node-network/node-network-states.hpp"
#include "match-manager-v2-tests.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include "utility-tests.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class NodeNetworkTestSuite : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(0);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, renderGlyph(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawButton(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());

        symbolWireless = new SymbolWirelessManager();
        symbolWireless->initialize(device.getWirelessManager(), device.getRemoteDeviceCoordinator());
        nodeNetwork = new NodeNetwork(&device, &wireless, symbolWireless);
    }

    void TearDown() override {
        delete nodeNetwork;
        nodeNetwork = nullptr;
        delete symbolWireless;
        symbolWireless = nullptr;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice device;
    FakeNodeNetworkWirelessManager wireless;
    SymbolWirelessManager* symbolWireless = nullptr;
    NodeNetwork* nodeNetwork = nullptr;
};

inline void nodeNetworkOwnsProviderAndMatchManager(NodeNetworkTestSuite* suite) {
    EXPECT_NE(suite->nodeNetwork->getPlayerProvider(), nullptr);
    EXPECT_NE(suite->nodeNetwork->getMatchManager(), nullptr);
    EXPECT_EQ(suite->nodeNetwork->getStateId(), NODE_NETWORK_APP_ID);
}

inline void nodeNetworkStartsInPlayerFetch(NodeNetworkTestSuite* suite) {
    suite->nodeNetwork->initialize(&suite->device);

    ASSERT_NE(suite->nodeNetwork->getCurrentState(), nullptr);
    EXPECT_EQ(suite->nodeNetwork->getCurrentState()->getStateId(), PLAYER_FETCH_APP_ID);
}

inline void nodeNetworkStaysInPlayerFetchBeforeWelcomeFinishes(NodeNetworkTestSuite* suite) {
    suite->nodeNetwork->initialize(&suite->device);

    suite->nodeNetwork->onStateLoop(&suite->device);

    EXPECT_EQ(suite->nodeNetwork->getCurrentState()->getStateId(), PLAYER_FETCH_APP_ID);
}

inline void nodeNetworkEntersNodeIdleWhenWelcomeFinishes(NodeNetworkTestSuite* suite) {
    suite->nodeNetwork->initialize(&suite->device);
    PlayerFetchApp* playerFetch = static_cast<PlayerFetchApp*>(suite->nodeNetwork->getCurrentState());
    ASSERT_TRUE(playerFetch->skipToState(&suite->device, 4));
    ASSERT_EQ(playerFetch->getCurrentState()->getStateId(), PlayerFetchStateId::WELCOME);

    suite->fakeClock->setTime(WELCOME_MESSAGE_TIMEOUT_MS + 1);
    suite->nodeNetwork->onStateLoop(&suite->device);
    suite->nodeNetwork->onStateLoop(&suite->device);

    EXPECT_EQ(suite->nodeNetwork->getCurrentState()->getStateId(), NODE_IDLE);
}

/** NodeIdle driven against a fake cable: port statuses and peer types are scripted per test. */
class NodeIdleLinkTestSuite : public testing::Test {
public:
    void SetUp() override {
        ON_CALL(*pdn.mockDisplay, invalidateScreen()).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawImage(_)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawText(_, _, _)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, setGlyphMode(_)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, render()).WillByDefault(Return());

        node = new NodeNetworkDevice(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM,
                                     NODE_NETWORK_TEST_MAC_A, DUEL_TEST_PEER_MAC,
                                     NODE_NETWORK_TEST_INITIATOR_MS);
        SimpleTimer::setPlatformClock(&node->clock);
        node->clock.setTime(0);
        stats = new NodeNetworkStats(&statsStorage);
        idle = new NodeIdle(node->provider.getPlayer(), node->manager, stats, &node->rdc);
    }

    void TearDown() override {
        delete idle;
        delete stats;
        delete node;
        SimpleTimer::setPlatformClock(nullptr);
    }

    void linkJack(SerialIdentifier jack, PortStatus status, DeviceType type = DeviceType::PDN) {
        node->rdc.setPortStatus(jack, status);
        node->rdc.setPeerDeviceType(jack, type);
    }

    size_t offersSent() const {
        size_t count = 0;
        for (const auto& sent : node->wireless.sent) {
            if (sent.packet.command == static_cast<uint8_t>(NodeNetworkCommand::OFFER)) {
                count++;
            }
        }
        return count;
    }

    MockDevice pdn;
    InMemoryStorage statsStorage;
    NodeNetworkDevice* node = nullptr;
    NodeNetworkStats* stats = nullptr;
    NodeIdle* idle = nullptr;
};

inline void nodeIdleOffersWhenOutputJackLinked(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->node->manager->getLocalLinkSide(), DuelLinkSide::INITIATOR);
    EXPECT_EQ(suite->offersSent(), 1u);
}

inline void nodeIdleWaitsAsResponderWhenInputJackLinked(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::INPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->node->manager->getLocalLinkSide(), DuelLinkSide::RESPONDER);
    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleIgnoresJackStillConnecting(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTING);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleIgnoresDaisyChainedJack(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::DAISY_CHAINED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleIgnoresPdnLinkedOnBothJacks(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    suite->linkJack(SerialIdentifier::INPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_EQ(suite->offersSent(), 0u);
    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleReoffersWhenOfferGoesUnanswered(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);

    suite->idle->onStateLoop(&suite->pdn);
    suite->node->clock.advance(500);
    suite->idle->onStateLoop(&suite->pdn);
    EXPECT_EQ(suite->offersSent(), 1u);

    suite->node->clock.advance(600);
    suite->idle->onStateLoop(&suite->pdn);
    EXPECT_EQ(suite->offersSent(), 2u);
    EXPECT_TRUE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleStopsOfferingWhenLinkDrops(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::CONNECTED);
    suite->idle->onStateLoop(&suite->pdn);
    ASSERT_TRUE(suite->node->manager->getSession().isActive());

    suite->linkJack(SerialIdentifier::OUTPUT_JACK, PortStatus::DISCONNECTED);
    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_FALSE(suite->node->manager->getSession().isActive());
}

inline void nodeIdleRoutesFdnToSymbolFlow(NodeIdleLinkTestSuite* suite) {
    suite->linkJack(SerialIdentifier::INPUT_JACK, PortStatus::CONNECTED, DeviceType::FDN);

    suite->idle->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->idle->transitionToSymbol());
    EXPECT_EQ(suite->offersSent(), 0u);
}

/** NodeDuelCountdown, NodeDuel, NodeScore, and NodeError driven against a scripted peer. */
class NodeDuelStateTestSuite : public testing::Test {
public:
    /** Step 0 lead used by scripted SEQUENCEs in duel tests. */
    static constexpr int32_t STEP_ZERO_LEAD_MS = 100;

    /** Builds a device with display stubs and captures the button callbacks NodeDuel registers. */
    void SetUp() override {
        ON_CALL(*pdn.mockDisplay, invalidateScreen()).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawImage(_)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawImage(_, _, _)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawImageInverted(_, _, _))
        .WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, drawText(_, _, _)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, setGlyphMode(_)).WillByDefault(Return(pdn.mockDisplay));
        ON_CALL(*pdn.mockDisplay, render()).WillByDefault(Return());
        ON_CALL(*pdn.mockPrimaryButton, setButtonPress(_, _, _))
        .WillByDefault(
        Invoke([this](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            primaryPress = fn;
            buttonCtx = ctx;
        }));
        ON_CALL(*pdn.mockSecondaryButton, setButtonPress(_, _, _))
        .WillByDefault(
        Invoke([this](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
            secondaryPress = fn;
            buttonCtx = ctx;
        }));

        node =
        new NodeNetworkDevice(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM, NODE_NETWORK_TEST_MAC_A,
                              DUEL_TEST_PEER_MAC, NODE_NETWORK_TEST_INITIATOR_MS);
        SimpleTimer::setPlatformClock(&node->clock);
        node->clock.setTime(1000);
        countdown = new NodeDuelCountdown(node->manager, &node->rdc);
        duel = new NodeDuel(node->manager, &node->rdc);
        score = new NodeScore(node->manager);
        error = new NodeError(node->manager);
    }

    /** Frees the states and device and detaches the test clock. */
    void TearDown() override {
        delete error;
        delete score;
        delete duel;
        delete countdown;
        delete node;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /** Completes the handshake with the local device as responder. */
    void handshakeAsResponder() {
        node->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
        NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
        offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;
        node->manager->onPacket(DUEL_TEST_PEER_MAC, offer);
        ASSERT_TRUE(node->manager->isHandshakeComplete());
        node->wireless.sent.clear();
    }

    /** Completes the handshake with the local device as initiator. */
    void handshakeAsInitiator() {
        node->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
        ASSERT_TRUE(node->manager->startMatch(DUEL_TEST_PEER_MAC));
        NodeNetworkPacket accept = makeNodeNetworkPacket(NodeNetworkCommand::ACCEPT, false);
        nodeNetworkPacketSetUuid(accept.matchId, node->manager->getSession().getMatchId());
        node->manager->onPacket(DUEL_TEST_PEER_MAC, accept);
        ASSERT_TRUE(node->manager->isHandshakeComplete());
        node->wireless.sent.clear();
    }

    /** @return a packet from the peer for the current match. */
    NodeNetworkPacket makePeerPacket(NodeNetworkCommand command) {
        const bool peerIsInitiator = node->manager->getLocalLinkSide() == DuelLinkSide::RESPONDER;
        NodeNetworkPacket packet = makeNodeNetworkPacket(command, peerIsInitiator);
        nodeNetworkPacketSetUuid(packet.matchId, node->manager->getSession().getMatchId());
        return packet;
    }

    /** Initiator: answers the latest SYNC probe with a zero-round-trip reply. */
    void answerLastProbe() {
        const std::vector<NodeNetworkPacket> probes = sentWith(NodeNetworkCommand::SYNC);
        ASSERT_FALSE(probes.empty());
        NodeNetworkPacket reply = makePeerPacket(NodeNetworkCommand::SYNC_REPLY);
        reply.syncOriginMs = probes.back().syncOriginMs;
        reply.syncReceiveMs = probes.back().syncOriginMs;
        reply.syncTransmitMs = probes.back().syncOriginMs;
        node->manager->onPacket(DUEL_TEST_PEER_MAC, reply);
    }

    /** Responder: delivers SEQUENCE with step 0 @p leadMs from now. */
    void deliverSequence(uint8_t buttonMask, uint8_t initiatorPresserMask,
                         int32_t leadMs = STEP_ZERO_LEAD_MS) {
        uint8_t prompts[NODE_NETWORK_PACKET_STEPS] = {};
        encodeSingleButtonPrompts(prompts, buttonMask, initiatorPresserMask);
        deliverPrompts(prompts, leadMs);
    }

    /** Responder: delivers SEQUENCE carrying raw prompt nibbles (initiator low, responder high). */
    void deliverPrompts(const uint8_t (&prompts)[NODE_NETWORK_PACKET_STEPS],
                        int32_t leadMs = STEP_ZERO_LEAD_MS) {
        NodeNetworkPacket packet = makePeerPacket(NodeNetworkCommand::SEQUENCE);
        memcpy(packet.stepPrompts, prompts, sizeof(prompts));
        packet.stepZeroAtMs =
        static_cast<uint32_t>(node->clock.milliseconds()) + static_cast<uint32_t>(leadMs);
        node->manager->onPacket(DUEL_TEST_PEER_MAC, packet);
        ASSERT_TRUE(node->manager->hasSequence());
    }

    /** Initiator: handshakes, records a good sync sample, and sends @p sequence. */
    void beginAsInitiator(const NodeInputSequence& sequence) {
        handshakeAsInitiator();
        node->manager->sendSyncProbe();
        answerLastProbe();
        ASSERT_TRUE(node->manager->beginRound(sequence));
        node->wireless.sent.clear();
    }

    /** Delivers the peer's INPUT_RESULT for @p step. */
    void deliverInputResult(uint8_t step, bool success, uint8_t pressCount = 1) {
        NodeNetworkPacket result = makePeerPacket(NodeNetworkCommand::INPUT_RESULT);
        result.stepIndex = step;
        result.inputSuccess = success ? 1 : 0;
        result.pressCount = pressCount;
        node->manager->onPacket(DUEL_TEST_PEER_MAC, result);
    }

    /** Delivers the peer's INPUT_PRESS for @p step. */
    void deliverInputPress(uint8_t step, uint8_t pressCount) {
        NodeNetworkPacket press = makePeerPacket(NodeNetworkCommand::INPUT_PRESS);
        press.stepIndex = step;
        press.pressCount = pressCount;
        node->manager->onPacket(DUEL_TEST_PEER_MAC, press);
    }

    /** Delivers the peer's ACK for the last sent packet carrying @p command. */
    void ackLastSent(NodeNetworkCommand command) {
        const std::vector<NodeNetworkPacket> packets = sentWith(command);
        ASSERT_FALSE(packets.empty());
        NodeNetworkPacket ack;
        ack.command = static_cast<uint8_t>(NodeNetworkCommand::ACK);
        ack.sequenceNumber = packets.back().sequenceNumber;
        memcpy(ack.matchId, packets.back().matchId, sizeof(ack.matchId));
        feedNodeNetworkPacket(node->wireless, DUEL_TEST_PEER_MAC, ack);
    }

    /** Moves the clock to the start of @p step and lets the duel catch up. */
    void runToStep(uint8_t step) {
        const int32_t wait = node->manager->msUntilStep(step);
        if (wait > 0) {
            node->clock.advance(static_cast<unsigned long>(wait));
        }
        for (int pass = 0; pass < 3; pass++) {
            duel->onStateLoop(&pdn);
        }
    }

    /** Runs the duel for @p ms in small ticks. */
    void loopDuelFor(unsigned long ms) {
        for (unsigned long elapsed = 0; elapsed < ms; elapsed += 25) {
            node->clock.advance(25);
            duel->onStateLoop(&pdn);
        }
    }

    /** Milliseconds from now until the last input window closes. */
    int32_t msUntilScheduleEnds() const {
        return node->manager->msUntilWindowCloses(NODE_SEQUENCE_LENGTH - 1);
    }

    /** Fires the captured callback for @p button. */
    void press(ButtonIdentifier button) {
        parameterizedCallbackFunction callback =
        button == ButtonIdentifier::PRIMARY_BUTTON ? primaryPress : secondaryPress;
        ASSERT_NE(callback, nullptr);
        callback(buttonCtx);
    }

    /** @return every sent packet carrying @p command, in send order. */
    std::vector<NodeNetworkPacket> sentWith(NodeNetworkCommand command) const {
        std::vector<NodeNetworkPacket> matches;
        for (const auto& sent : node->wireless.sent) {
            if (sent.packet.command == static_cast<uint8_t>(command)) {
                matches.push_back(sent.packet);
            }
        }
        return matches;
    }

    MockDevice pdn;
    NodeNetworkDevice* node = nullptr;
    NodeDuelCountdown* countdown = nullptr;
    NodeDuel* duel = nullptr;
    NodeScore* score = nullptr;
    NodeError* error = nullptr;
    parameterizedCallbackFunction primaryPress = nullptr;
    parameterizedCallbackFunction secondaryPress = nullptr;
    void* buttonCtx = nullptr;
};

// ============================================
// NodeDuelCountdown
// ============================================

inline void nodeCountdownInitiatorSyncsThenSendsSequence(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsInitiator();
    suite->countdown->onStateMounted(&suite->pdn);

    for (uint8_t probe = 0; probe < NODE_SYNC_PROBE_COUNT; probe++) {
        suite->countdown->onStateLoop(&suite->pdn);
        ASSERT_EQ(suite->sentWith(NodeNetworkCommand::SYNC).size(), probe + 1u);
        suite->answerLastProbe();
        suite->node->clock.advance(NODE_SYNC_PROBE_INTERVAL_MS + 1);
    }
    EXPECT_TRUE(suite->sentWith(NodeNetworkCommand::SEQUENCE).empty());
    suite->node->clock.advance(NODE_SYNC_SETTLE_MS);
    suite->countdown->onStateLoop(&suite->pdn);

    const std::vector<NodeNetworkPacket> sequences = suite->sentWith(NodeNetworkCommand::SEQUENCE);
    ASSERT_EQ(sequences.size(), 1u);
    EXPECT_EQ(suite->node->manager->msUntilStep(0), static_cast<int32_t>(NODE_COUNTDOWN_MS));
    NodeNetworkPacket ack;
    ack.command = static_cast<uint8_t>(NodeNetworkCommand::ACK);
    ack.sequenceNumber = sequences[0].sequenceNumber;
    memcpy(ack.matchId, sequences[0].matchId, sizeof(ack.matchId));
    feedNodeNetworkPacket(suite->node->wireless, DUEL_TEST_PEER_MAC, ack);

    suite->node->clock.advance(NODE_COUNTDOWN_MS - 1);
    suite->countdown->onStateLoop(&suite->pdn);
    EXPECT_FALSE(suite->countdown->shallWeBattle());
    suite->node->clock.advance(1);
    suite->countdown->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->countdown->shallWeBattle());
    EXPECT_FALSE(suite->countdown->abortedToError());
    suite->countdown->onStateDismounted(&suite->pdn);
    EXPECT_TRUE(suite->node->manager->getSession().isActive());
}

inline void nodeCountdownInitiatorAbortsWithoutSync(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsInitiator();
    suite->countdown->onStateMounted(&suite->pdn);

    for (uint8_t probe = 0; probe < NODE_SYNC_PROBE_COUNT; probe++) {
        suite->countdown->onStateLoop(&suite->pdn);
        suite->node->clock.advance(NODE_SYNC_PROBE_INTERVAL_MS + 1);
    }
    suite->node->clock.advance(NODE_SYNC_SETTLE_MS);
    suite->countdown->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->countdown->abortedToError());
    EXPECT_FALSE(suite->countdown->shallWeBattle());
    EXPECT_EQ(suite->sentWith(NodeNetworkCommand::ABORT).size(), 1u);
    EXPECT_TRUE(suite->sentWith(NodeNetworkCommand::SEQUENCE).empty());
    EXPECT_EQ(suite->node->manager->getFailedAttempts(), 0u);
    suite->countdown->onStateDismounted(&suite->pdn);
}

inline void nodeCountdownInitiatorAbortsWhenSequenceUnacked(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsInitiator();
    suite->countdown->onStateMounted(&suite->pdn);
    for (uint8_t probe = 0; probe < NODE_SYNC_PROBE_COUNT; probe++) {
        suite->countdown->onStateLoop(&suite->pdn);
        suite->answerLastProbe();
        suite->node->clock.advance(NODE_SYNC_PROBE_INTERVAL_MS + 1);
    }
    suite->node->clock.advance(NODE_SYNC_SETTLE_MS);
    suite->countdown->onStateLoop(&suite->pdn);
    ASSERT_EQ(suite->sentWith(NodeNetworkCommand::SEQUENCE).size(), 1u);

    for (int tick = 0; tick < NODE_RELIABLE_MAX_ATTEMPTS; tick++) {
        suite->node->clock.advance(NODE_RELIABLE_RETRY_MS);
        suite->node->wireless.update();
        suite->countdown->onStateLoop(&suite->pdn);
    }

    EXPECT_TRUE(suite->countdown->abortedToError());
    EXPECT_FALSE(suite->countdown->shallWeBattle());
    suite->countdown->onStateDismounted(&suite->pdn);
}

inline void nodeCountdownResponderAbortsWithoutSequence(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->countdown->onStateMounted(&suite->pdn);

    suite->node->clock.advance(NODE_SEQUENCE_WAIT_MS);
    suite->countdown->onStateLoop(&suite->pdn);
    EXPECT_FALSE(suite->countdown->abortedToError());
    suite->node->clock.advance(1);
    suite->countdown->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->countdown->abortedToError());
    EXPECT_EQ(suite->sentWith(NodeNetworkCommand::ABORT).size(), 1u);
    suite->countdown->onStateDismounted(&suite->pdn);
}

inline void nodeCountdownResponderBattlesAtStepZero(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->countdown->onStateMounted(&suite->pdn);
    suite->countdown->onStateLoop(&suite->pdn);

    suite->deliverSequence(0, 0, static_cast<int32_t>(NODE_COUNTDOWN_MS));
    suite->countdown->onStateLoop(&suite->pdn);
    suite->node->clock.advance(NODE_COUNTDOWN_MS - 1);
    suite->countdown->onStateLoop(&suite->pdn);
    EXPECT_FALSE(suite->countdown->shallWeBattle());
    suite->node->clock.advance(1);
    suite->countdown->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->countdown->shallWeBattle());
    suite->countdown->onStateDismounted(&suite->pdn);
}

inline void nodeCountdownAbortFromOpponentRoutesToError(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->countdown->onStateMounted(&suite->pdn);

    suite->node->manager->onPacket(DUEL_TEST_PEER_MAC,
                                   suite->makePeerPacket(NodeNetworkCommand::ABORT));
    suite->countdown->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->countdown->abortedToError());
    suite->countdown->onStateDismounted(&suite->pdn);
    EXPECT_TRUE(suite->node->manager->isAborted());
}

// ============================================
// NodeDuel
// ============================================

inline void nodeDuelResponderCorrectPressReportsSuccess(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x01, 0x00);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);

    suite->press(ButtonIdentifier::SECONDARY_BUTTON);

    const std::vector<NodeNetworkPacket> results =
    suite->sentWith(NodeNetworkCommand::INPUT_RESULT);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].stepIndex, 0);
    EXPECT_EQ(results[0].inputSuccess, 1);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelPresserSeesStageResultAfterPress(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x00);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(_, _, _)).Times(::testing::AnyNumber());
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("Stage"), _, _))
    .WillOnce(Return(suite->pdn.mockDisplay));
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("1/5"), _, _))
    .WillOnce(Return(suite->pdn.mockDisplay));
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("PASS"), _, _))
    .WillOnce(Return(suite->pdn.mockDisplay));

    suite->press(ButtonIdentifier::PRIMARY_BUTTON);
    suite->duel->onStateLoop(&suite->pdn);

    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelViewerStageResultFillsInWhenReported(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x01);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(_, _, _)).Times(::testing::AnyNumber());
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("PASS"), _, _)).Times(0);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("1/5"), _, _))
    .Times(2)
    .WillRepeatedly(Return(suite->pdn.mockDisplay));
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("MISS"), _, _))
    .WillOnce(Return(suite->pdn.mockDisplay));

    suite->node->clock.advance(NODE_INPUT_WINDOW_MS + 1);
    suite->duel->onStateLoop(&suite->pdn);
    suite->deliverInputResult(0, false);
    suite->duel->onStateLoop(&suite->pdn);
    suite->duel->onStateLoop(&suite->pdn);

    suite->duel->onStateDismounted(&suite->pdn);
}

inline void
nodeDuelResponderWrongPressFailsAndLaterPressesAreIgnored(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x00);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);

    suite->press(ButtonIdentifier::SECONDARY_BUTTON);
    suite->press(ButtonIdentifier::PRIMARY_BUTTON);

    const std::vector<NodeNetworkPacket> results =
    suite->sentWith(NodeNetworkCommand::INPUT_RESULT);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].inputSuccess, 0);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelPressBeforeStepIsIgnored(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x00);
    suite->duel->onStateMounted(&suite->pdn);
    suite->duel->onStateLoop(&suite->pdn);

    suite->press(ButtonIdentifier::PRIMARY_BUTTON);

    EXPECT_TRUE(suite->sentWith(NodeNetworkCommand::INPUT_RESULT).empty());
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelResponderTimeoutFailsInput(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x00);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);

    suite->node->clock.advance(NODE_INPUT_WINDOW_MS);
    suite->duel->onStateLoop(&suite->pdn);
    suite->press(ButtonIdentifier::PRIMARY_BUTTON);

    const std::vector<NodeNetworkPacket> results =
    suite->sentWith(NodeNetworkCommand::INPUT_RESULT);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].stepIndex, 0);
    EXPECT_EQ(results[0].inputSuccess, 0);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelResponderViewerPressIsIgnored(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x01);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);

    suite->press(ButtonIdentifier::PRIMARY_BUTTON);
    suite->node->clock.advance(NODE_INPUT_WINDOW_MS);
    suite->duel->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->sentWith(NodeNetworkCommand::INPUT_RESULT).empty());
    suite->duel->onStateDismounted(&suite->pdn);
}

/** Step 0 is the local responder's primary-then-secondary prompt; the rest are the initiator's. */
inline constexpr uint8_t NODE_DUEL_TEST_RESPONDER_DOUBLE[NODE_NETWORK_PACKET_STEPS] = {
0xA0, 0x01, 0x01, 0x01, 0x01};

inline void nodeDuelDoublePromptNeedsBothPressesInOrder(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverPrompts(NODE_DUEL_TEST_RESPONDER_DOUBLE);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImageInverted(_, 0, -1)).Times(1);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImageInverted(_, 64, -1)).Times(0);

    suite->press(ButtonIdentifier::PRIMARY_BUTTON);
    suite->duel->onStateLoop(&suite->pdn);

    const std::vector<NodeNetworkPacket> presses = suite->sentWith(NodeNetworkCommand::INPUT_PRESS);
    ASSERT_EQ(presses.size(), 1u);
    EXPECT_EQ(presses[0].stepIndex, 0);
    EXPECT_EQ(presses[0].pressCount, 1);
    EXPECT_TRUE(suite->sentWith(NodeNetworkCommand::INPUT_RESULT).empty());

    suite->press(ButtonIdentifier::SECONDARY_BUTTON);

    const std::vector<NodeNetworkPacket> results =
    suite->sentWith(NodeNetworkCommand::INPUT_RESULT);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].inputSuccess, 1);
    EXPECT_EQ(results[0].pressCount, 2);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelDoublePromptWrongFirstPressEndsStep(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverPrompts(NODE_DUEL_TEST_RESPONDER_DOUBLE);
    suite->duel->onStateMounted(&suite->pdn);
    suite->runToStep(0);

    suite->press(ButtonIdentifier::SECONDARY_BUTTON);
    suite->press(ButtonIdentifier::SECONDARY_BUTTON);

    const std::vector<NodeNetworkPacket> results =
    suite->sentWith(NodeNetworkCommand::INPUT_RESULT);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].inputSuccess, 0);
    EXPECT_EQ(results[0].pressCount, 1);
    EXPECT_TRUE(suite->sentWith(NodeNetworkCommand::INPUT_PRESS).empty());
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelViewerSeesTwoButtonsAndOpponentPresses(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    const uint8_t prompts[NODE_NETWORK_PACKET_STEPS] = {0x0A, 0x01, 0x01, 0x01, 0x01};
    suite->deliverPrompts(prompts);
    suite->duel->onStateMounted(&suite->pdn);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImage(_)).Times(0);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImage(_, 0, -1)).Times(::testing::AtLeast(1));
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImage(_, 64, -1)).Times(::testing::AtLeast(1));
    suite->runToStep(0);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImageInverted(_, 0, -1)).Times(1);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImageInverted(_, 64, -1)).Times(0);

    suite->deliverInputPress(0, 1);
    suite->duel->onStateLoop(&suite->pdn);
    suite->duel->onStateLoop(&suite->pdn);

    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelBothPressStepShowsOwnPromptAndNeedsBoth(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    const uint8_t prompts[NODE_NETWORK_PACKET_STEPS] = {0xA1, 0x01, 0x01, 0x01, 0x01};
    suite->deliverPrompts(prompts);
    suite->duel->onStateMounted(&suite->pdn);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImage(_)).Times(0);
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImage(_, 0, -1)).Times(::testing::AtLeast(1));
    EXPECT_CALL(*suite->pdn.mockDisplay, drawImage(_, 64, -1)).Times(::testing::AtLeast(1));
    suite->runToStep(0);

    suite->press(ButtonIdentifier::PRIMARY_BUTTON);
    suite->press(ButtonIdentifier::SECONDARY_BUTTON);
    const DuelSession& session = suite->node->manager->getSession();
    EXPECT_EQ(session.getStepResult(0), NodeStepResult::PENDING);

    suite->deliverInputResult(0, true);

    EXPECT_EQ(session.getStepResult(0), NodeStepResult::SUCCESS);
    const std::vector<NodeNetworkPacket> results =
    suite->sentWith(NodeNetworkCommand::INPUT_RESULT);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].inputSuccess, 1);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelResponderConcludesOnOutcome(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x1F);
    suite->duel->onStateMounted(&suite->pdn);
    NodeNetworkPacket outcome = suite->makePeerPacket(NodeNetworkCommand::OUTCOME);
    outcome.initiatorScore = 4;
    outcome.responderScore = 0;
    suite->node->manager->onPacket(DUEL_TEST_PEER_MAC, outcome);

    suite->loopDuelFor(static_cast<unsigned long>(suite->msUntilScheduleEnds()) + 50);

    EXPECT_TRUE(suite->duel->transitionToScore());
    EXPECT_TRUE(suite->node->manager->wasLastRoundSuccess());
    EXPECT_EQ(suite->node->manager->getLastRoundScore(), 4);
    EXPECT_EQ(suite->node->manager->getPendingRecordCount(), 1u);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelResponderWithoutOutcomeAborts(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x1F);
    suite->duel->onStateMounted(&suite->pdn);
    suite->deliverInputResult(0, true);
    const unsigned long schedule = static_cast<unsigned long>(suite->msUntilScheduleEnds());

    suite->loopDuelFor(schedule + NODE_OUTCOME_WAIT_MS - 100);
    EXPECT_FALSE(suite->duel->abortedToError());
    suite->loopDuelFor(200);

    EXPECT_TRUE(suite->duel->abortedToError());
    EXPECT_FALSE(suite->duel->transitionToScore());
    EXPECT_EQ(suite->node->manager->getFailedAttempts(), 0u);
    EXPECT_EQ(suite->sentWith(NodeNetworkCommand::ABORT).size(), 1u);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelInitiatorRunsFullRound(NodeDuelStateTestSuite* suite) {
    const NodeInputSequence sequence =
    makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER);
    suite->beginAsInitiator(sequence);
    suite->duel->onStateMounted(&suite->pdn);

    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        suite->runToStep(step);
        if (suite->node->manager->getSession().isLocalPresser(step)) {
            suite->press(sequence.inputs[step].initiator.buttons[0]);
        } else {
            suite->deliverInputResult(step, true);
        }
    }
    EXPECT_FALSE(suite->duel->transitionToScore());
    suite->loopDuelFor(NODE_INPUT_WINDOW_MS + 50);
    EXPECT_FALSE(suite->duel->transitionToScore());
    suite->ackLastSent(NodeNetworkCommand::OUTCOME);
    suite->duel->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->duel->transitionToScore());
    EXPECT_EQ(suite->sentWith(NodeNetworkCommand::INPUT_RESULT).size(), 3u);
    const std::vector<NodeNetworkPacket> outcomes = suite->sentWith(NodeNetworkCommand::OUTCOME);
    ASSERT_EQ(outcomes.size(), 1u);
    EXPECT_EQ(outcomes[0].initiatorScore, 3);
    EXPECT_EQ(outcomes[0].responderScore, 2);
    EXPECT_EQ(suite->node->manager->getLastRoundScore(), 5);
    EXPECT_TRUE(suite->node->manager->wasLastRoundSuccess());
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelInitiatorExpiresMissingResults(NodeDuelStateTestSuite* suite) {
    suite->beginAsInitiator(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::RESPONDER));
    suite->duel->onStateMounted(&suite->pdn);
    const unsigned long schedule = static_cast<unsigned long>(suite->msUntilScheduleEnds());

    suite->loopDuelFor(schedule + NODE_RESULT_GRACE_MS - 100);
    EXPECT_TRUE(suite->sentWith(NodeNetworkCommand::OUTCOME).empty());
    suite->loopDuelFor(200);
    suite->ackLastSent(NodeNetworkCommand::OUTCOME);
    suite->duel->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->duel->transitionToScore());
    EXPECT_EQ(suite->node->manager->getLastRoundScore(), 0);
    EXPECT_FALSE(suite->node->manager->wasLastRoundSuccess());
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelInitiatorAbortsWhenOutcomeUnacked(NodeDuelStateTestSuite* suite) {
    suite->beginAsInitiator(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::RESPONDER));
    suite->duel->onStateMounted(&suite->pdn);
    suite->loopDuelFor(static_cast<unsigned long>(suite->msUntilScheduleEnds()) +
                       NODE_RESULT_GRACE_MS + 50);
    ASSERT_EQ(suite->sentWith(NodeNetworkCommand::OUTCOME).size(), 1u);

    for (uint8_t attempt = 0; attempt < NODE_RELIABLE_MAX_ATTEMPTS; attempt++) {
        suite->node->clock.advance(NODE_RELIABLE_RETRY_MS);
        suite->node->wireless.update();
        suite->duel->onStateLoop(&suite->pdn);
    }

    EXPECT_TRUE(suite->duel->abortedToError());
    EXPECT_FALSE(suite->duel->transitionToScore());
    EXPECT_EQ(suite->node->manager->getFailedAttempts(), 0u);
    EXPECT_EQ(suite->node->manager->getPendingRecordCount(), 0u);
    suite->duel->onStateDismounted(&suite->pdn);
}

inline void nodeDuelSessionDropAborts(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    suite->deliverSequence(0x00, 0x00);
    suite->duel->onStateMounted(&suite->pdn);

    suite->node->manager->clearCurrentMatch();
    suite->duel->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->duel->abortedToError());
    EXPECT_FALSE(suite->duel->transitionToScore());
    EXPECT_EQ(suite->node->manager->getFailedAttempts(), 0u);
    suite->duel->onStateDismounted(&suite->pdn);
}

// ============================================
// NodeScore and NodeError
// ============================================

inline void nodeScoreShowsScoreThenRoutesBySuccess(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsResponder();
    NodeNetworkPacket outcome = suite->makePeerPacket(NodeNetworkCommand::OUTCOME);
    outcome.initiatorScore = 1;
    outcome.responderScore = 2;
    suite->node->manager->onPacket(DUEL_TEST_PEER_MAC, outcome);
    suite->node->manager->concludeRound();
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(_, _, _)).Times(::testing::AnyNumber());
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("3/5"), _, _))
    .WillOnce(Return(suite->pdn.mockDisplay));

    suite->score->onStateMounted(&suite->pdn);
    EXPECT_FALSE(suite->score->transitionToLinkRetry());

    suite->node->clock.advance(3000);
    suite->score->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->score->transitionToLinkRetry());
    EXPECT_FALSE(suite->score->transitionToWin());
    suite->score->onStateDismounted(&suite->pdn);
}

inline void nodeErrorShowsRetryingThenReturnsToIdle(NodeDuelStateTestSuite* suite) {
    suite->handshakeAsInitiator();
    suite->node->manager->abortRound();
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(_, _, _)).Times(::testing::AnyNumber());
    EXPECT_CALL(*suite->pdn.mockDisplay, drawText(::testing::StrEq("Retrying"), _, _))
    .WillOnce(Return(suite->pdn.mockDisplay));

    suite->error->onStateMounted(&suite->pdn);
    EXPECT_FALSE(suite->error->transitionToIdle());
    suite->node->clock.advance(NodeError::DISPLAY_DURATION_MS + 1);
    suite->error->onStateLoop(&suite->pdn);

    EXPECT_TRUE(suite->error->transitionToIdle());
    suite->error->onStateDismounted(&suite->pdn);
    EXPECT_FALSE(suite->node->manager->isAborted());
    EXPECT_EQ(suite->node->manager->getFailedAttempts(), 0u);
}

inline void nodeNetworkIgnoresOfferBeforeLinkSideIsAssigned() {
    NodeNetworkDevice responder(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM,
                                NODE_NETWORK_TEST_MAC_A, DUEL_TEST_PEER_MAC,
                                NODE_NETWORK_TEST_RESPONDER_MS);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    responder.manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_TRUE(responder.wireless.sent.empty());
    EXPECT_FALSE(responder.manager->getSession().isActive());
}

inline void nodeNetworkRegistersOnlyItsOwnPacketHandler(NodeNetworkTestSuite* suite) {
    delete suite->nodeNetwork;
    suite->nodeNetwork = nullptr;
    SymbolWirelessManager symbolWireless;
    symbolWireless.initialize(suite->device.getWirelessManager(),
                              suite->device.getRemoteDeviceCoordinator());
    EXPECT_CALL(*suite->device.mockPeerComms, setPacketHandler(PktType::kNodeNetworkCommand, _, _))
        .Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms, setPacketHandler(PktType::kSymbolMatchCommand, _, _))
        .Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms, clearPacketHandler(PktType::kNodeNetworkCommand))
        .Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms, clearPacketHandler(PktType::kSymbolMatchCommand))
        .Times(1);

    NodeNetwork* nodeNetwork = new NodeNetwork(&suite->device, &suite->wireless, &symbolWireless);
    delete nodeNetwork;
}