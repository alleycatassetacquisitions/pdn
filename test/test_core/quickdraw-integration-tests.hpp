#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "utility-tests.hpp"
#include "wireless/direct-peer-table.hpp"
#include "device/wireless-manager.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::DoAll;

using TestQuickdrawPacket = QuickdrawPacket;

// ============================================
// Packet Creation Helpers
// ============================================

inline TestQuickdrawPacket createTestPacket(
    const std::string& matchId,
    const std::string& playerId,
    int command,
    long playerDrawTime = 0,
    bool isHunter = true
) {
    // Zero-init so seqId is 0 (fire-and-forget): these helpers model unreliable
    // sends, and the channel dedups reliable (nonzero-seqId) packets per sender.
    TestQuickdrawPacket packet{};
    strncpy(packet.matchId, matchId.c_str(), 36);
    packet.matchId[36] = '\0';
    strncpy(packet.playerId, playerId.c_str(), 4);
    packet.playerId[4] = '\0';
    packet.isHunter = isHunter;
    packet.playerDrawTime = playerDrawTime;
    packet.command = command;
    return packet;
}

// Creates a packet as if the specified player is sending their draw time.
// Pass asHunter=true when the hunter is the sender, false when the bounty is.
inline TestQuickdrawPacket createTestPacketFromMatch(const std::optional<Match>& match, int command, bool asHunter) {
    long time = asHunter ? match->getHunterDrawTime() : match->getBountyDrawTime();
    const char* pid = asHunter ? match->getHunterId() : match->getBountyId();
    return createTestPacket(match->getMatchId(), pid, command, time, asHunter);
}

// ============================================
// Base Test Fixture for Single-Device Tests
// ============================================

class SingleDeviceTestFixture : public testing::Test {
public:
    static constexpr const char* DEFAULT_HUNTER_ID = "hunt";
    static constexpr const char* DEFAULT_BOUNTY_ID = "boun";
    static constexpr const char* DEFAULT_OPPONENT_MAC = "AA:BB:CC:DD:EE:FF";
    static constexpr long DEFAULT_START_TIME = 10000;

    void SetUp() override {
        setupClock();
        setupPlayer();
        setupManagers();
        setupDefaultMockExpectations();
    }

    void TearDown() override {
        cleanupManagers();
        cleanupPlayer();
        cleanupClock();
    }

    // Returns the real match id assigned by initializeMatch(). Tests that
    // build packets via createPacket use this, so listenForMatchEvents'
    // matchId check against the active match succeeds.
    virtual std::string getMatchId() const {
        return matchManager && matchManager->getCurrentMatch().has_value()
            ? matchManager->getCurrentMatch()->getMatchId()
            : std::string("");
    }

    virtual void setupDefaultMockExpectations() {}

    // Creates a packet as if sent by the local player's opponent.
    TestQuickdrawPacket createPacket(int command, long hunterTime = 0, long bountyTime = 0) {
        if (player->isHunter()) {
            // Opponent is the bounty: send bountyTime with isHunter=false
            return createTestPacket(getMatchId(), DEFAULT_BOUNTY_ID, command, bountyTime, false);
        } else {
            // Opponent is the hunter: send hunterTime with isHunter=true
            return createTestPacket(getMatchId(), DEFAULT_HUNTER_ID, command, hunterTime, true);
        }
    }

    void processPacket(const TestQuickdrawPacket& packet,
                      const uint8_t macAddr[6] = nullptr) {
        uint8_t defaultMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        const uint8_t* mac = macAddr ? macAddr : defaultMac;
        transport->deliverIncoming(
            PktType::kQuickdrawCommand, 0, mac,
            reinterpret_cast<const uint8_t*>(&packet),
            sizeof(packet)
        );
    }

    // All members are public for access from standalone test functions
    MockStorage storage;
    Player* player = nullptr;
    MatchManager* matchManager = nullptr;
    QuickdrawTestStack wireless;
    WirelessManager* deviceWirelessManager = nullptr;  // alias into `wireless`
    WirelessTransport* transport = nullptr;            // alias into `wireless`
    FakeRemoteDeviceCoordinator fakeRdc;
    FakePlatformClock* fakeClock = nullptr;

private:
    void setupClock() {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(DEFAULT_START_TIME);
    }

    void setupPlayer() {
        player = new Player();
        char playerId[] = "hunt";
        player->setUserID(playerId);
        player->setIsHunter(true);
    }

    void setupManagers() {
        deviceWirelessManager = &wireless.wirelessManager;
        transport = &wireless.transport;
        matchManager = new MatchManager();
        matchManager->initialize(player, &storage, transport);
        // Stub the RDC with the opponent MAC this fixture uses so the
        // SEND_MATCH_ID gate in listenForMatchEvents passes for delivered packets.
        {
            uint8_t stubMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
            fakeRdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, stubMac);
            fakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, stubMac);
            matchManager->setRemoteDeviceCoordinator(&fakeRdc);
        }

        // No ACK is routed back (single-device tests don't need matchIsReady).
        // The opponent MAC must match what processPacket delivers from, or
        // listenForMatchEvents' source-MAC gate drops the packet.
        uint8_t opponentMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        matchManager->initializeMatch(opponentMac);
        matchManager->setDuelLocalStartTime(DEFAULT_START_TIME);
    }

    void cleanupManagers() {
        matchManager->clearCurrentMatch();
        delete matchManager;
    }

    void cleanupPlayer() {
        delete player;
    }

    void cleanupClock() {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }
};

// ============================================
// Base Test Fixture for Two-Device Tests
// ============================================

class TwoDeviceTestFixture : public testing::Test {
public:
    static constexpr const char* DEFAULT_HUNTER_ID = "hunt";
    static constexpr const char* DEFAULT_BOUNTY_ID = "boun";
    static constexpr long DEFAULT_START_TIME = 10000;

    void SetUp() override {
        setupClock();
        setupHunter();
        setupBounty();
        setupMatches();
        setupDefaultMockExpectations();
    }

    void TearDown() override {
        cleanupMatches();
        cleanupBounty();
        cleanupHunter();
        cleanupClock();
    }

    virtual std::string getMatchId() const { return "two-device-match-id-123456"; }

    virtual void setupDefaultMockExpectations() {}

    void hunterSendsToBounty(int command) {
        TestQuickdrawPacket packet = createTestPacketFromMatch(
            hunterMatchManager->getCurrentMatch(), command, true);
        uint8_t macAddr[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        bountyTransport->deliverIncoming(
            PktType::kQuickdrawCommand, 0, macAddr,
            reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    void bountySendsToHunter(int command) {
        TestQuickdrawPacket packet = createTestPacketFromMatch(
            bountyMatchManager->getCurrentMatch(), command, false);
        uint8_t macAddr[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        hunterTransport->deliverIncoming(
            PktType::kQuickdrawCommand, 0, macAddr,
            reinterpret_cast<uint8_t*>(&packet), sizeof(packet)
        );
    }

    // All members are public for access from standalone test functions
    Player* hunter = nullptr;
    Player* bounty = nullptr;
    MatchManager* hunterMatchManager = nullptr;
    MatchManager* bountyMatchManager = nullptr;
    QuickdrawTestStack hunterWireless;
    QuickdrawTestStack bountyWireless;
    WirelessTransport* hunterTransport = nullptr;  // alias into hunterWireless
    WirelessTransport* bountyTransport = nullptr;  // alias into bountyWireless
    MockStorage hunterStorage;
    MockStorage bountyStorage;
    FakeRemoteDeviceCoordinator hunterFakeRdc;
    FakeRemoteDeviceCoordinator bountyFakeRdc;
    FakePlatformClock* fakeClock = nullptr;

private:
    void setupClock() {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(DEFAULT_START_TIME);
    }

    void setupHunter() {
        hunter = new Player();
        char hunterId[] = "hunt";
        hunter->setUserID(hunterId);
        hunter->setIsHunter(true);

        hunterTransport = &hunterWireless.transport;
        hunterMatchManager = new MatchManager();
        hunterMatchManager->initialize(hunter, &hunterStorage, hunterTransport);
        hunterMatchManager->setRemoteDeviceCoordinator(&hunterFakeRdc);
    }

    void setupBounty() {
        bounty = new Player();
        char bountyId[] = "boun";
        bounty->setUserID(bountyId);
        bounty->setIsHunter(false);

        bountyTransport = &bountyWireless.transport;
        bountyMatchManager = new MatchManager();
        bountyMatchManager->initialize(bounty, &bountyStorage, bountyTransport);
        bountyMatchManager->setRemoteDeviceCoordinator(&bountyFakeRdc);
    }

    void setupMatches() {
        // Full production handshake:
        //   1. Hunter broadcasts SEND_MATCH_ID  → bounty receives, creates match, sends MATCH_ID_ACK
        //   2. Bounty broadcasts MATCH_ID_ACK   → hunter receives, sets matchIsReady
        uint8_t hunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
        // Stub each side's RDC direct peer with the other side's MAC so the
        // SEND_MATCH_ID gate accepts the delivered packets.
        hunterFakeRdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, bountyMac);
        bountyFakeRdc.setPeerMac(SerialIdentifier::INPUT_JACK, hunterMac);
        hunterMatchManager->initializeMatch(bountyMac);
        hunterWireless.deliverLastTo(&bountyWireless.transport, hunterMac);
        bountyWireless.deliverLastTo(&hunterWireless.transport, bountyMac);

        hunterMatchManager->setDuelLocalStartTime(DEFAULT_START_TIME);
        bountyMatchManager->setDuelLocalStartTime(DEFAULT_START_TIME);
    }

    void cleanupMatches() {
        hunterMatchManager->clearCurrentMatch();
        bountyMatchManager->clearCurrentMatch();
    }

    void cleanupHunter() {
        delete hunterMatchManager;
        delete hunter;
    }

    void cleanupBounty() {
        delete bountyMatchManager;
        delete bounty;
    }

    void cleanupClock() {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }
};

// ============================================
// Packet Parsing Tests
// ============================================

class PacketParsingTests : public SingleDeviceTestFixture {
public:
};

inline void packetParsingRejectsMalformedPacket(PacketParsingTests* suite) {
    // A truncated DRAW_RESULT must be dropped by the channel without
    // dispatching: it must not resolve the opponent's side of the duel.
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 250);
    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    suite->transport->deliverIncoming(
        PktType::kQuickdrawCommand, 0, macAddr,
        reinterpret_cast<const uint8_t*>(&packet), sizeof(packet) - 1);

    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
}

// A reliable packet (nonzero seqId) resent after a lost ack must be dispatched
// exactly once: the channel dedups per sender. A later packet with a fresh
// seqId is a new command and must dispatch again.
inline void packetParsingDedupsResentReliablePacket(PacketParsingTests* suite) {
    // Counted on a standalone stack: in the fixture the channel's onReceive is
    // MatchManager's handler, whose dispatch count isn't observable. The dedup
    // under test lives in the channel, identical on both stacks.
    QuickdrawTestStack stack;
    int dispatches = 0;
    auto* channel = stack.transport.channel<QuickdrawPacket>(
        PktType::kQuickdrawCommand, [](uint8_t, const uint8_t*) {});
    channel->onReceive(
        [&](const uint8_t*, const QuickdrawPacket&) { ++dispatches; });

    uint8_t macAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 250);
    packet.seqId = 7;

    auto deliver = [&](const TestQuickdrawPacket& p) {
        stack.transport.deliverIncoming(
            PktType::kQuickdrawCommand, 0, macAddr,
            reinterpret_cast<const uint8_t*>(&p), sizeof(p));
    };

    deliver(packet);            // first arrival
    deliver(packet);            // lost-ack retransmit, same seqId
    EXPECT_EQ(dispatches, 1);

    packet.seqId = 8;           // next command from same sender
    deliver(packet);
    EXPECT_EQ(dispatches, 2);
}

inline void listenForMatchResultsSetsOpponentTimeHunter(PacketParsingTests* suite) {
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 350);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 350);
}

inline void listenForMatchResultsSetsOpponentTimeBounty(PacketParsingTests* suite) {
    suite->player->setIsHunter(false);
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 200, 0);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 200);
}

inline void listenForMatchResultsHandlesNeverPressed(PacketParsingTests* suite) {
    // We pressed, but slower than the opponent's recorded time. The opponent
    // then reports NEVER_PRESSED. didWin must still be true: a timed-out opponent
    // loses regardless of times; the pressed=false branch short-circuits the
    // time comparison. This is what distinguishes NEVER_PRESSED from DRAW_RESULT.
    suite->matchManager->setHunterDrawTime(50000);
    suite->matchManager->setReceivedButtonPush();

    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 0, 9999);
    suite->processPacket(packet);

    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 9999);
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin())
        << "opponent NEVER_PRESSED => win even though our 50000ms > their 9999ms";
}

inline void listenForMatchResultsIgnoresUnexpectedCommands(PacketParsingTests* suite) {
    // A SEND_MATCH_ID from a MAC that isn't our RDC peer must be ignored: it must
    // neither register a draw result nor hijack the already-active match.
    const std::string originalMatchId = suite->matchManager->getCurrentMatch()->getMatchId();
    static const char kStrangerMatchId[37] = "000000000000000000000000000000000000";
    ASSERT_NE(originalMatchId, kStrangerMatchId);

    uint8_t strangerMac[6] = {0x99, 0x99, 0x99, 0x99, 0x99, 0x99};
    QuickdrawPacket cmd = makeQuickdrawPacket(
        QDCommand::SEND_MATCH_ID, kStrangerMatchId, "test", 0, true);

    suite->matchManager->listenForMatchEvents(strangerMac, cmd);

    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getMatchId(), originalMatchId)
        << "stranger SEND_MATCH_ID must not replace the active match";
}

// ============================================
// Callback Chain Tests
// ============================================

class CallbackChainTests : public SingleDeviceTestFixture {
public:
    void setupDefaultMockExpectations() override {
        SingleDeviceTestFixture::setupDefaultMockExpectations();
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    MockDevice device;
};

inline void callbackChainPacketToStateFlag(CallbackChainTests* suite) {
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 180);
    
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    suite->processPacket(packet);
    EXPECT_TRUE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getBountyDrawTime(), 180);
}

inline void callbackChainButtonPressCalculatesTime(CallbackChainTests* suite) {
    suite->fakeClock->advance(275);
    
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    ASSERT_NE(buttonCallback, nullptr);
    
    buttonCallback(suite->matchManager);
    
    EXPECT_TRUE(suite->matchManager->getHasPressedButton());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 275);
}

inline void callbackChainButtonMasherPenalty(CallbackChainTests* suite) {
    auto masherCallback = suite->matchManager->getButtonMasher();
    masherCallback(suite->matchManager);
    masherCallback(suite->matchManager);
    masherCallback(suite->matchManager);
    
    suite->fakeClock->advance(200);
    
    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    buttonCallback(suite->matchManager);
    
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 425);
}

inline void callbackChainButtonPressBroadcasts(CallbackChainTests* suite) {
    suite->fakeClock->advance(150);

    size_t sentBefore = suite->wireless.sent().size();

    auto buttonCallback = suite->matchManager->getDuelButtonPush();
    buttonCallback(suite->matchManager);

    ASSERT_GT(suite->wireless.sent().size(), sentBefore);
    EXPECT_EQ(suite->wireless.sent().back().packet.command, QDCommand::DRAW_RESULT);
}

// ============================================
// State Flow Integration Tests
// ============================================

class StateFlowIntegrationTests : public SingleDeviceTestFixture {
public:
    void SetUp() override {
        SingleDeviceTestFixture::SetUp();
        chainManager = new ChainManager(player, deviceWirelessManager, &device.fakeRemoteDeviceCoordinator);
    }

    void TearDown() override {
        delete chainManager;
        SingleDeviceTestFixture::TearDown();
    }

    void setupDefaultMockExpectations() override {
        SingleDeviceTestFixture::setupDefaultMockExpectations();
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockHaptics, getIntensity()).WillByDefault(Return(0));
        ON_CALL(storage, write(_, _)).WillByDefault(Return(100));
        ON_CALL(storage, writeUChar(_, _)).WillByDefault(Return(1));
        ON_CALL(storage, readUChar(_, _)).WillByDefault(Return(0));
    }

    MockDevice device;
    ChainManager* chainManager = nullptr;
};

inline void stateFlowDutPressesFirstWins(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(150);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelPushed());
    
    DuelPushed pushedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    pushedState.onStateMounted(&suite->device);
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 150, 300);
    suite->processPacket(packet);
    
    pushedState.onStateLoop(&suite->device);
    EXPECT_TRUE(pushedState.transitionToDuelResult());
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin());
}

inline void stateFlowDutReceivesFirstLoses(StateFlowIntegrationTests* suite) {
    // Both Duel::onStateMounted and DuelReceivedResult::onStateMounted call setButtonPress.
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(2);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(2);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 100);
    suite->processPacket(packet);
    
    duelState.onStateLoop(&suite->device);
    EXPECT_TRUE(duelState.transitionToDuelReceivedResult());
    
    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(350);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    receivedState.onStateLoop(&suite->device);
    EXPECT_TRUE(receivedState.transitionToDuelResult());
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin());
}

// Regression: a press landing the same iteration the button-push grace expires
// must count, not be reported as NEVER_PRESSED. execDrivers() runs the button
// handler before onStateLoop, so my side is already resolved (pressed) when the
// timer expires at one onStateLoop visit. sendNeverPressed must no-op there
// rather than resolve my side as a timeout and lose a duel the player won.
inline void stateFlowPressAtGraceExpiryStillCounts(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(2);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(2);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);

    // Opponent (bounty) drew very slowly, so a press of any in-window time wins.
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 9000);
    suite->processPacket(packet);

    duelState.onStateLoop(&suite->device);
    ASSERT_TRUE(duelState.transitionToDuelReceivedResult());

    DuelReceivedResult receivedState(suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator);
    receivedState.onStateMounted(&suite->device);

    // Advance past the grace window, then press, then run the loop, all without
    // a transition check between, exactly the same-iteration firmware ordering.
    suite->fakeClock->advance(800);  // past the 750ms button-push grace window
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    receivedState.onStateLoop(&suite->device);

    // The press must win, and no NEVER_PRESSED may have been broadcast.
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin());
    for (const auto& sent : suite->wireless.sent()) {
        EXPECT_NE(sent.packet.command, QDCommand::NEVER_PRESSED)
            << "NEVER_PRESSED broadcast despite a registered press";
    }
}

inline void stateFlowDutNeverPressesLoses(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 0, 100);
    suite->processPacket(packet);

    // DUT never presses in time: press lands after pity time.
    suite->fakeClock->advance(9999);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_FALSE(suite->matchManager->didWin());
}

inline void stateFlowOpponentNeverRespondsWins(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);

    suite->fakeClock->advance(150);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);

    TestQuickdrawPacket packet = suite->createPacket(QDCommand::NEVER_PRESSED, 150, 9999);
    suite->processPacket(packet);
    
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(suite->matchManager->didWin());
}

inline void stateFlowThroughDuelResultToWin(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(100);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 100, 200);
    suite->processPacket(packet);
    
    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    
    // Allow display methods to be called (don't require specific calls)
    ON_CALL(*suite->device.mockDisplay, drawText(_, _, _))
        .WillByDefault(Return(suite->device.mockDisplay));
    
    resultState.onStateMounted(&suite->device);
    resultState.onStateLoop(&suite->device);
    
    EXPECT_TRUE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

inline void stateFlowThroughDuelResultToLose(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    Duel duelState(suite->player, suite->matchManager, &suite->device.fakeRemoteDeviceCoordinator, suite->chainManager, nullptr);
    duelState.onStateMounted(&suite->device);
    
    suite->fakeClock->advance(300);
    suite->matchManager->getDuelButtonPush()(suite->matchManager);
    
    TestQuickdrawPacket packet = suite->createPacket(QDCommand::DRAW_RESULT, 300, 150);
    suite->processPacket(packet);
    
    DuelResult resultState(suite->player, suite->matchManager, nullptr);
    
    // Allow display methods to be called (don't require specific calls)
    ON_CALL(*suite->device.mockDisplay, drawText(_, _, _))
        .WillByDefault(Return(suite->device.mockDisplay));
    
    resultState.onStateMounted(&suite->device);
    resultState.onStateLoop(&suite->device);
    
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_TRUE(resultState.transitionToLose());
}

// Pins the venue-stats split: shootout outcomes never write lifetime Player
// stats. A shootout-prefixed match driven through the duel flow and DuelResult
// must leave wins / losses / streak / matchesPlayed and the reaction-time log
// untouched on BOTH outcomes. The
// write sites all gate on currentMatchIsShootout() (DuelResult::onStateMounted
// and MatchManager's addReactionTime calls); un-gating any of them fails here.
inline void stateFlowShootoutMatchWritesNoLifetimeStats(StateFlowIntegrationTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    ON_CALL(*suite->device.mockDisplay, drawText(_, _, _))
        .WillByDefault(Return(suite->device.mockDisplay));

    // Seed non-zero counters so a wrongful resetStreak() is visible too.
    suite->player->incrementMatchesPlayed();
    suite->player->incrementWins();
    suite->player->incrementStreak();
    const int wins = suite->player->getWins();
    const int losses = suite->player->getLosses();
    const int streak = suite->player->getStreak();
    const int played = suite->player->getMatchesPlayed();
    const unsigned long lastReaction = suite->player->getLastReactionTime();

    uint8_t opponentMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    auto runShootoutDuel = [&](long localMs, long opponentMs) {
        suite->matchManager->clearCurrentMatch();
        std::string id = std::string(kShootoutMatchIdPrefix) + "stats-guard";
        suite->matchManager->initializeShootoutMatch(id.c_str(), opponentMac, true);
        Duel duelState(suite->player, suite->matchManager,
                       &suite->device.fakeRemoteDeviceCoordinator,
                       suite->chainManager, nullptr);
        duelState.onStateMounted(&suite->device);
        suite->fakeClock->advance(localMs);
        suite->matchManager->getDuelButtonPush()(suite->matchManager);
        TestQuickdrawPacket packet =
            suite->createPacket(QDCommand::DRAW_RESULT, 0, opponentMs);
        suite->processPacket(packet);
        DuelResult resultState(suite->player, suite->matchManager, nullptr);
        resultState.onStateMounted(&suite->device);
    };

    runShootoutDuel(100, 200);  // local wins
    runShootoutDuel(300, 150);  // local loses

    EXPECT_EQ(suite->player->getWins(), wins);
    EXPECT_EQ(suite->player->getLosses(), losses);
    EXPECT_EQ(suite->player->getStreak(), streak);
    EXPECT_EQ(suite->player->getMatchesPlayed(), played);
    EXPECT_EQ(suite->player->getLastReactionTime(), lastReaction);
}

// ============================================
// Two-Device Simulation Tests
// ============================================

class TwoDeviceSimulationTests : public TwoDeviceTestFixture {
public:
    std::string getMatchId() const override { return "two-device-match-id-123456"; }
};

inline void twoDeviceHunterPressesFirstBothAgree(TwoDeviceSimulationTests* suite) {
    suite->fakeClock->advance(150);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    suite->fakeClock->advance(130);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    EXPECT_TRUE(suite->hunterMatchManager->didWin());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());
}

inline void twoDeviceBountyPressesFirstBothAgree(TwoDeviceSimulationTests* suite) {
    suite->fakeClock->advance(120);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    suite->fakeClock->advance(100);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);
    
    EXPECT_FALSE(suite->hunterMatchManager->didWin());
    EXPECT_TRUE(suite->bountyMatchManager->didWin());
}

inline void twoDeviceCloseRaceCorrectWinner(TwoDeviceSimulationTests* suite) {
    // Very close race - hunter at 200ms, bounty at 201ms
    suite->fakeClock->advance(200);
    suite->hunterMatchManager->getDuelButtonPush()(suite->hunterMatchManager);
    
    suite->fakeClock->advance(1);
    suite->bountyMatchManager->getDuelButtonPush()(suite->bountyMatchManager);
    
    suite->hunterSendsToBounty(QDCommand::DRAW_RESULT);
    suite->bountySendsToHunter(QDCommand::DRAW_RESULT);

    EXPECT_TRUE(suite->hunterMatchManager->didWin());
    EXPECT_FALSE(suite->bountyMatchManager->didWin());
}
