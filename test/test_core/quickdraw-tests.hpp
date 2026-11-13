#pragma once

#include <array>
#include <memory>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "id-generator.hpp"
#include "game/game-session.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-apps.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "game/player-provider.hpp"
#include "device/firmware-update-state.hpp"
#include "utility-tests.hpp"
#include "fake-firmware-store.hpp"
#include "protocol-constants.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::NiceMock;
using ::testing::DoAll;

// Stubs a fixture's RDC to accept the conventional test MAC as a direct peer
// so MatchManager's SEND_MATCH_ID gate passes.
inline void stubFixtureRdcDirectPeer(MockDevice& device) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK, dummyMac);
    device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::OUTPUT_JACK, dummyMac);
}

static const uint8_t kTestMacBytes[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// Stands a ShootoutManager up to MATCH_IN_PROGRESS with this device in the match
// against the coordinator. `shootout` must have been built on the fixture's
// MatchManager, which it then primes the way MATCH_START does on hardware.
// `selfMac` sorts above the coordinator's, which puts the local side in the
// BOUNTY draw slot — the opposite of the fixtures' standing hunter role, so a
// reader consulting the wrong one of the two is visible. Returns the opponent's
// MAC.
inline std::array<uint8_t, 6>
mountShootoutMatchWithBountySlot(ShootoutManager& shootout, MockDevice& device, uint8_t* selfMac) {
    std::array<uint8_t, 6> me{};
    memcpy(me.data(), selfMac, 6);
    std::array<uint8_t, 6> coordinator = {0x01, 0, 0, 0, 0, 0};
    std::array<uint8_t, 6> spectator = {0x07, 0, 0, 0, 0, 0};
    ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(selfMac));
    ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));

    device.fakeRemoteDeviceCoordinator.ringHeadMac = coordinator;
    shootout.onRingClosedReceived(coordinator.data(), {me, coordinator, spectator});
    shootout.startProposal();
    for (const std::array<uint8_t, 6>& m : {me, coordinator, spectator}) {
        shootout.onConfirmReceived(m.data());
    }
    shootout.onBracketReceived(coordinator.data(), {me, coordinator, spectator}, 1);
    shootout.onMatchStartReceived(coordinator.data(), me.data(), coordinator.data(), 0, 2);
    return coordinator;
}

// ============================================
// Idle State Tests
// ============================================

class IdleStateTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        char playerId[] = "1234";
        player->setUserID(playerId);
        player->setIsHunter(true);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        stubFixtureRdcDirectPeer(device);
        matchManager = new MatchManager(player, &storage, device.wirelessManager,
                                        &device.fakeRemoteDeviceCoordinator);

        posseManager = new PosseManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

        ctx.player = player;
        ctx.matchManager = matchManager;
        ctx.remoteDeviceCoordinator = &device.fakeRemoteDeviceCoordinator;
        ctx.posseManager = posseManager;
        idleState = new Idle(ctx);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        delete idleState;
        delete posseManager;
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    Player* player;
    MatchManager* matchManager;
    PosseManager* posseManager;
    Idle* idleState;
    GameContext ctx;
    FakePlatformClock* fakeClock;
};

// Test: Idle state mounts and registers button callbacks
inline void idleMountRegistersButtonCallbacks(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    // With FakeRemoteDeviceCoordinator always returning DISCONNECTED, should not transition
    EXPECT_FALSE(suite->idleState->transitionToMatchCountdown());
}

// Test: Idle state does not transition without a connection
inline void idleDoesNotTransitionWhenDisconnected(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToMatchCountdown());

    suite->idleState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->idleState->transitionToMatchCountdown());
}

// Test: State cleanup on dismount
inline void idleStateClearsOnDismount(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    suite->idleState->onStateDismounted(&suite->device);
}

// Test: Button callbacks are registered and removed properly
inline void idleButtonCallbacksRegisteredAndRemoved(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    suite->idleState->onStateMounted(&suite->device);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    suite->idleState->onStateDismounted(&suite->device);
}

inline void pairingCodeFirmwareHoldStartsAnUpdate(IdleStateTests* suite) {
    ON_CALL(*suite->device.mockDisplay, renderGlyph(_, _, _)).WillByDefault(Return(suite->device.mockDisplay));
    PlayerProvider playerProvider;
    PinEntryState pinEntry(&playerProvider);
    pinEntry.onStateMounted(&suite->device);
    suite->fakeClock->advance(PLAYER_FETCH_FIRMWARE_HOLD_MS + 1000);

    EXPECT_CALL(*suite->device.mockSecondaryButton, longPressedMillis())
    .WillOnce(Return(PLAYER_FETCH_FIRMWARE_HOLD_MS - 1))
    .WillOnce(Return(PLAYER_FETCH_FIRMWARE_HOLD_MS));
    pinEntry.onStateLoop(&suite->device);
    EXPECT_FALSE(pinEntry.transitionToFirmwareUpdate());
    pinEntry.onStateLoop(&suite->device);
    EXPECT_TRUE(pinEntry.transitionToFirmwareUpdate());

    pinEntry.onStateDismounted(&suite->device);
    EXPECT_FALSE(pinEntry.transitionToFirmwareUpdate());
}

// The button's pressed time counts from press-down, so a hold that began before the
// mount, even one not yet a long press then, is longer than the screen has been up.
inline void pairingCodeFirmwareHoldCarriedInDoesNotFire(IdleStateTests* suite) {
    ON_CALL(*suite->device.mockDisplay, renderGlyph(_, _, _)).WillByDefault(Return(suite->device.mockDisplay));
    PlayerProvider playerProvider;
    PinEntryState pinEntry(&playerProvider);
    pinEntry.onStateMounted(&suite->device);
    suite->fakeClock->advance(PLAYER_FETCH_FIRMWARE_HOLD_MS - 500);

    EXPECT_CALL(*suite->device.mockSecondaryButton, longPressedMillis())
    .WillOnce(Return(PLAYER_FETCH_FIRMWARE_HOLD_MS))
    .WillOnce(Return(PLAYER_FETCH_FIRMWARE_HOLD_MS));
    pinEntry.onStateLoop(&suite->device);
    EXPECT_FALSE(pinEntry.transitionToFirmwareUpdate());
    suite->fakeClock->advance(PLAYER_FETCH_FIRMWARE_HOLD_MS + 2000);
    pinEntry.onStateLoop(&suite->device);
    EXPECT_TRUE(pinEntry.transitionToFirmwareUpdate());
    pinEntry.onStateDismounted(&suite->device);
}

// The pairing screen takes firmware updates for as long as it is up.
inline void pairingCodeTakesUpdatesWhileUp(IdleStateTests* suite) {
    ON_CALL(*suite->device.mockDisplay, renderGlyph(_, _, _)).WillByDefault(Return(suite->device.mockDisplay));
    PeerCommsInterface::PacketCallback onFrame;
    EXPECT_CALL(*suite->device.mockPeerComms, setPacketHandler(PktType::kFirmwareUpdate, _, _))
        .WillOnce(SaveArg<1>(&onFrame));
    FakeFirmwareStore store;
    FirmwareUpdateManager manager(suite->device.mockPeerComms, &store);
    suite->device.firmwareUpdateManager = &manager;
    std::vector<uint8_t> chunkZero(sizeof(FirmwareChunkHeader) + FIRMWARE_CHUNK_SIZE, 0);
    FirmwareChunkHeader header{};
    header.imageLength = 2 * FIRMWARE_CHUNK_SIZE;
    std::memcpy(chunkZero.data(), &header, sizeof(header));

    onFrame(nullptr, chunkZero.data(), chunkZero.size(), nullptr);
    EXPECT_EQ(store.beginWriteCalls, 0) << "nothing is taken before the screen is up";

    PlayerProvider playerProvider;
    PinEntryState pinEntry(&playerProvider);
    pinEntry.onStateMounted(&suite->device);
    onFrame(nullptr, chunkZero.data(), chunkZero.size(), nullptr);
    EXPECT_EQ(store.beginWriteCalls, 1);

    pinEntry.onStateDismounted(&suite->device);
    EXPECT_EQ(store.abortCalls, 1) << "leaving the screen drops the receive";
    suite->device.firmwareUpdateManager = nullptr;
}

// The pairing screen leaves the radio to whoever switched it to WiFi.
inline void pairingCodeLeavesTheRadioAlone(IdleStateTests* suite) {
    ON_CALL(*suite->device.mockDisplay, renderGlyph(_, _, _)).WillByDefault(Return(suite->device.mockDisplay));
    suite->device.wirelessManager->enableWifiMode();
    PlayerProvider playerProvider;
    PinEntryState pinEntry(&playerProvider);
    pinEntry.onStateMounted(&suite->device);
    EXPECT_EQ(suite->device.wirelessManager->getCurrentMode(), WirelessMode::WIFI);
    pinEntry.onStateDismounted(&suite->device);
}

inline void aSeedRunTakesTheRadioForEspNow(IdleStateTests* suite) {
    suite->device.wirelessManager->enableWifiMode();
    FakeFirmwareStore store;
    FirmwareUpdateManager manager(suite->device.mockPeerComms, &store);
    FirmwareUpdate seedScreen(PlayerFetchStateId::FIRMWARE_UPDATE, &manager);
    seedScreen.onStateMounted(&suite->device);
    EXPECT_EQ(suite->device.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW);
    seedScreen.onStateDismounted(&suite->device);
}

// Test: transitionToMatchCountdown stays false while match exists but ACK not yet received
inline void idleDoesNotTransitionWithMatchButNotReady(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    suite->idleState->onStateMounted(&suite->device);

    // Hunter initiates but has not yet received MATCH_ID_ACK
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(suite->idleState->transitionToMatchCountdown());
}

// Test: transitionToMatchCountdown returns true once matchIsReady is set via the full handshake
inline void idleTransitionsToMatchCountdownWhenMatchIsReady(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    suite->idleState->onStateMounted(&suite->device);

    // Hunter initiates match, then receives ACK from bounty
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    const char* matchId = suite->matchManager->getCurrentMatch()->getMatchId();
    MatchCommand ack(dummyMac, MatchCmd::MATCH_ID_ACK, matchId, 0, false);
    suite->matchManager->listenForMatchEvents(ack);

    EXPECT_TRUE(suite->idleState->transitionToMatchCountdown());
}

inline void idleDismountClearsUnreadyMatch(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    suite->idleState->onStateMounted(&suite->device);

    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    ASSERT_FALSE(suite->matchManager->isMatchReady());

    suite->idleState->onStateDismounted(&suite->device);
    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
}

// A cabled hunter and bounty look like a match waiting to start. While a
// tournament is live the bracket pairs the players, so Idle starts none of its
// own, and it hears the tournament end without being remounted.
inline void idleStartsNoMatchWhileATournamentIsLive(IdleStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    uint8_t bountyMac[6] = {0x0B, 0x00, 0x00, 0x00, 0x00, 0x01};
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, bountyMac);
    rdc.setPeerRole(SerialIdentifier::OUTPUT_JACK, false);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);

    ShootoutManager shootout(suite->player, suite->device.wirelessManager, &rdc, nullptr);
    suite->ctx.shootoutManager = &shootout;
    Idle idleState(suite->ctx);
    mountState(&idleState, &suite->device);

    shootout.startProposal();
    idleState.onStateLoop(&suite->device);
    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value())
    << "Idle started a match of its own inside a tournament";

    shootout.resetToIdle();
    idleState.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value())
    << "Idle went on refusing matches after the tournament was over";

    dismountState(&idleState, &suite->device);
    suite->ctx.shootoutManager = nullptr;
}

// ============================================
// Countdown State Tests
// ============================================

class MatchCountdownTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        stubFixtureRdcDirectPeer(device);
        matchManager = new MatchManager(player, &storage, device.wirelessManager,
                                        &device.fakeRemoteDeviceCoordinator);
        uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        matchManager->initializeMatch(dummyMac);

        posseManager = new PosseManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

        ctx.player = player;
        ctx.matchManager = matchManager;
        ctx.remoteDeviceCoordinator = &device.fakeRemoteDeviceCoordinator;
        ctx.posseManager = posseManager;
        countdownState = new MatchCountdown(ctx);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete countdownState;
        delete posseManager;
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Helper to capture button callback
    parameterizedCallbackFunction capturedButtonCallback = nullptr;
    void* capturedContext = nullptr;

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    Player* player;
    MatchManager* matchManager;
    PosseManager* posseManager;
    MatchCountdown* countdownState;
    GameContext ctx;
    FakePlatformClock* fakeClock;
};

inline unsigned long measureDrawPenaltyMs(MatchCountdownTests* suite) {
    suite->matchManager->setMatchLocalStartTime(suite->fakeClock->milliseconds());
    suite->matchManager->getMatchButtonPush()(suite->matchManager);
    return suite->matchManager->getCurrentMatch()->getHunterDrawTime();
}

inline void countdownButtonMasherPenaltyIncrementsOnButtonPress(MatchCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    suite->countdownState->onStateMounted(&suite->device);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    EXPECT_EQ(measureDrawPenaltyMs(suite), 75u);
}

inline void countdownMultipleEarlyPressesAccumulatePenalty(MatchCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    suite->countdownState->onStateMounted(&suite->device);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::PRESS);
    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);

    EXPECT_EQ(measureDrawPenaltyMs(suite), 225u);
}

inline void countdownBindsMasherOnPressAndResetsStaleCount(MatchCountdownTests* suite) {
    suite->matchManager->getButtonMasher()(suite->matchManager);
    suite->matchManager->getButtonMasher()(suite->matchManager);
    suite->matchManager->getButtonMasher()(suite->matchManager);

    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    suite->countdownState->onStateMounted(&suite->device);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::CLICK);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::CLICK);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::PRESS);
    EXPECT_EQ(measureDrawPenaltyMs(suite), 150u);
}

inline void countdownDismountDetachesMasher(MatchCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    suite->countdownState->onStateMounted(&suite->device);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::PRESS);

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    suite->countdownState->onStateDismounted(&suite->device);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::PRESS);

    Idle idleState(suite->ctx);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, ButtonInteraction::CLICK))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, ButtonInteraction::CLICK))
    .Times(1);
    idleState.onStateMounted(&suite->device);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::PRESS);
    EXPECT_EQ(measureDrawPenaltyMs(suite), 75u);
}

// Test: Countdown progresses through stages
inline void countdownProgressesThroughStages(MatchCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    suite->countdownState->onStateMounted(&suite->device);
    
    // Initially should not trigger battle
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through THREE stage (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through TWO stage (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through ONE stage (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    
    // After ONE expires, BATTLE should trigger
    EXPECT_TRUE(suite->countdownState->shallWeBattle());
}

// Test: Battle transition sets flag
inline void countdownBattleTransitionSetsFlag(MatchCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    suite->countdownState->onStateMounted(&suite->device);
    
    // Should not battle initially
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // Advance through each stage properly:
    // THREE -> TWO (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // TWO -> ONE (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
    
    // ONE -> BATTLE (2000ms)
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    
    EXPECT_TRUE(suite->countdownState->shallWeBattle());
}

// Test: Countdown cleanup on dismount
inline void countdownCleansUpOnDismount(MatchCountdownTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    suite->countdownState->onStateMounted(&suite->device);
    
    // Advance through each stage to trigger battle
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    suite->fakeClock->advance(2100);
    suite->countdownState->onStateLoop(&suite->device);
    
    EXPECT_TRUE(suite->countdownState->shallWeBattle());
    
    // Expect callbacks to be removed
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    suite->countdownState->onStateDismounted(&suite->device);
    
    // Flag should be reset
    EXPECT_FALSE(suite->countdownState->shallWeBattle());
}

// ============================================
// Match State Tests
// ============================================

class MatchStateTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(10000); // Start at 10 seconds

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);

        wirelessManager = new QuickdrawRadioTap();
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        wirelessManager->attach(device.mockPeerComms);
        stubFixtureRdcDirectPeer(device);
        matchManager = new MatchManager(player, &storage, device.wirelessManager,
                                        &device.fakeRemoteDeviceCoordinator);

        posseManager = new PosseManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

        // Create a match for testing via the production wireless path
        uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        matchManager->initializeMatch(dummyMac);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));

        ctx.player = player;
        ctx.matchManager = matchManager;
        ctx.remoteDeviceCoordinator = &device.fakeRemoteDeviceCoordinator;
        ctx.posseManager = posseManager;
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete posseManager;
        delete matchManager;
        delete wirelessManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    QuickdrawRadioTap* wirelessManager;
    Player* player;
    MatchManager* matchManager;
    PosseManager* posseManager;
    GameContext ctx;
    FakePlatformClock* fakeClock;
};

// The shootout match timeout forfeits the match's HUNTER. Which side that is comes
// from the match's draw slot, not the standing role: the two disagree for the whole
// match, and reading the standing role here sends the wrong device to reportLocalWin.
inline void matchShootoutTimeoutForfeitsTheMatchHunter(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
        .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    suite->matchManager->clearCurrentMatch();
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    ShootoutManager shootout(suite->player, suite->device.wirelessManager,
                             &suite->device.fakeRemoteDeviceCoordinator, suite->matchManager);
    std::array<uint8_t, 6> opponent =
    mountShootoutMatchWithBountySlot(shootout, suite->device, selfMac);
    suite->ctx.shootoutManager = &shootout;
    ASSERT_TRUE(suite->player->isHunter()) << "the standing role should be the opposite";
    ASSERT_FALSE(suite->matchManager->isLocalHunter());

    MatchState matchState(suite->ctx);
    matchState.onStateMounted(&suite->device);
    suite->fakeClock->advance(5000);  // past MatchState::MATCH_TIMEOUT
    matchState.onStateLoop(&suite->device);

    EXPECT_TRUE(matchState.transitionToShootoutSpectator())
    << "the match's bounty did not take the forfeit win";
    EXPECT_FALSE(matchState.transitionToShootoutEliminated());
    EXPECT_TRUE(shootout.isEliminated(opponent.data()));

    suite->ctx.shootoutManager = nullptr;
}

// ============================================
// Scenario 1: DUT presses button first, then receives result
// ============================================

// Test: Button press transitions to MatchPushed
inline void matchButtonPressTransitionsToMatchPushed(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    MatchState matchState(suite->ctx);
    matchState.onStateMounted(&suite->device);

    // Initially should not transition
    EXPECT_FALSE(matchState.transitionToMatchPushed());

    // Simulate button press via match manager callback
    suite->fakeClock->advance(200); // 200ms reaction time
    suite->matchManager->getMatchButtonPush()(suite->matchManager);

    matchState.onStateLoop(&suite->device);

    EXPECT_TRUE(matchState.transitionToMatchPushed());
}

// Test: Button press calculates reaction time correctly
inline void matchButtonPressCalculatesReactionTime(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    MatchState matchState(suite->ctx);
    matchState.onStateMounted(&suite->device);

    // Advance 250ms (simulating reaction time)
    suite->fakeClock->advance(250);
    
    // Trigger button press
    suite->matchManager->getMatchButtonPush()(suite->matchManager);

    // Check that hunter draw time was set
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 250);
}

// Test: Button press applies button masher penalty
inline void matchButtonPressAppliesMasherPenalty(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    // First, simulate button mashing during countdown
    MatchCountdown countdownState(suite->ctx);

    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);

    countdownState.onStateMounted(&suite->device);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::PRESS);

    // Cycle through THREE → TWO → ONE → BATTLE (3 × 2001ms) so the battle edge
    // holds and the match survives the dismount into the match.
    // SimpleTimer::expired() uses strict '<', so we need elapsed > duration (2000ms).
    for (int i = 0; i < 3; i++) {
        suite->fakeClock->advance(2001);
        countdownState.onStateLoop(&suite->device);
    }

    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    countdownState.onStateDismounted(&suite->device);

    MatchState matchState(suite->ctx);

    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    matchState.onStateMounted(&suite->device);

    // Advance 200ms
    suite->fakeClock->advance(200);

    suite->device.mockSecondaryButton->trigger(ButtonInteraction::PRESS);

    // Reaction time should be 200ms + (2 * 75ms penalty) = 350ms
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 350);
}

inline void matchLaterCommandSupersedesEarlierRetries(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    auto countOf = [suite](int command) {
        size_t n = 0;
        for (const MatchPacket& p : suite->wirelessManager->sentFrames) {
            if (p.command == command) n++;
        }
        return n;
    };

    ASSERT_EQ(countOf(MatchCmd::SEND_MATCH_ID), 1u);

    suite->fakeClock->advance(200);
    suite->matchManager->getMatchButtonPush()(suite->matchManager);
    ASSERT_EQ(countOf(MatchCmd::DRAW_RESULT), 1u);

    for (int i = 0; i < 4; i++) {
        suite->fakeClock->advance(150);
        suite->device.wirelessManager->sync();
    }
    EXPECT_EQ(countOf(MatchCmd::SEND_MATCH_ID), 1u);
    EXPECT_GT(countOf(MatchCmd::DRAW_RESULT), 1u);
}

inline void matchButtonPressBroadcastsDrawResult(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    size_t sentBefore = suite->wirelessManager->sentFrames.size();

    MatchState matchState(suite->ctx);
    matchState.onStateMounted(&suite->device);

    suite->fakeClock->advance(200);
    suite->matchManager->getMatchButtonPush()(suite->matchManager);

    ASSERT_GT(suite->wirelessManager->sentFrames.size(), sentBefore);
    EXPECT_EQ(suite->wirelessManager->sentFrames.back().command, MatchCmd::DRAW_RESULT);
}

inline void matchScoresOnButtonPressAndIgnoresClick(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    MatchState matchState(suite->ctx);
    matchState.onStateMounted(&suite->device);

    suite->fakeClock->advance(150);
    suite->device.mockPrimaryButton->trigger(ButtonInteraction::CLICK);
    suite->device.mockSecondaryButton->trigger(ButtonInteraction::CLICK);
    matchState.onStateLoop(&suite->device);
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
    EXPECT_FALSE(matchState.transitionToMatchPushed());

    suite->fakeClock->advance(50);
    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    matchState.onStateLoop(&suite->device);

    EXPECT_TRUE(suite->matchManager->getHasPressedButton());
    EXPECT_TRUE(matchState.transitionToMatchPushed());
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_EQ(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 200u);
}

inline void matchPushedWaitsForOpponentResult(MatchStateTests* suite) {
    MatchPushed pushedState(suite->ctx);

    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    pushedState.onStateMounted(&suite->device);
    
    // Should not transition immediately
    EXPECT_FALSE(pushedState.transitionToMatchResult());

    suite->fakeClock->advance(500);
    pushedState.onStateLoop(&suite->device);
    EXPECT_FALSE(pushedState.transitionToMatchResult());

    suite->fakeClock->advance(500);
    pushedState.onStateLoop(&suite->device);
    EXPECT_FALSE(pushedState.transitionToMatchResult());
}

// Test: MatchPushed transitions when result received
inline void matchPushedTransitionsOnResultReceived(MatchStateTests* suite) {
    MatchPushed pushedState(suite->ctx);

    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    
    pushedState.onStateMounted(&suite->device);
    
    // Simulate receiving opponent's result
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedDrawResult();
    
    // Should now have all results
    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(pushedState.transitionToMatchResult());
}

// ============================================
// Scenario 2: DUT receives result first, then presses button
// ============================================

// Test: Received result transitions to MatchReceivedResult
inline void matchReceivedResultTransitionsToMatchReceivedResult(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    MatchState matchState(suite->ctx);
    matchState.onStateMounted(&suite->device);

    // Simulate receiving opponent's result before pressing button
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    matchState.onStateLoop(&suite->device);

    EXPECT_TRUE(matchState.transitionToMatchReceivedResult());
}

// Test: MatchReceivedResult waits for button press
inline void matchReceivedResultWaitsForButtonPress(MatchStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    MatchReceivedResult receivedState(suite->ctx);
    receivedState.onStateMounted(&suite->device);
    
    // Should not transition immediately
    EXPECT_FALSE(receivedState.transitionToMatchResult());

    // Advance less than grace period (750ms)
    suite->fakeClock->advance(400);
    receivedState.onStateLoop(&suite->device);
    
    // Still waiting (either for button press or grace period expiry)
    // The transition depends on matchResultsAreIn() or the internal flag
}

// Test: Button press during grace period causes transition
inline void matchButtonPressDuringGracePeriodTransitions(MatchStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    EXPECT_CALL(*suite->device.mockPrimaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton,
                setButtonPress(_, suite->matchManager, ButtonInteraction::PRESS))
    .Times(1);

    MatchReceivedResult receivedState(suite->ctx);
    receivedState.onStateMounted(&suite->device);

    suite->fakeClock->advance(300);

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::CLICK);
    receivedState.onStateLoop(&suite->device);
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
    EXPECT_FALSE(receivedState.transitionToMatchResult());

    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    receivedState.onStateLoop(&suite->device);

    EXPECT_TRUE(suite->matchManager->matchResultsAreIn());
    EXPECT_TRUE(receivedState.transitionToMatchResult());
}

// ============================================
// Scenario 3: Neither device presses (timeout)
// ============================================

// Test: Match timeout transitions to idle
inline void matchTimeoutTransitionsToIdle(MatchStateTests* suite) {
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    MatchState matchState(suite->ctx);
    matchState.onStateMounted(&suite->device);

    // Initially should not timeout
    EXPECT_FALSE(matchState.transitionToIdle());

    // Advance past timeout (4000ms)
    suite->fakeClock->advance(4100);
    matchState.onStateLoop(&suite->device);

    EXPECT_TRUE(matchState.transitionToIdle());
}

// ============================================
// Scenario 4: DUT presses, opponent never responds
// ============================================

// Test: MatchPushed grace period expires and transitions
inline void matchPushedGracePeriodExpiresTransitions(MatchStateTests* suite) {
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);

    MatchPushed pushedState(suite->ctx);
    pushedState.onStateMounted(&suite->device);

    suite->fakeClock->advance(1500);
    pushedState.onStateLoop(&suite->device);
    EXPECT_FALSE(pushedState.transitionToMatchResult());

    suite->fakeClock->advance(1100);
    pushedState.onStateLoop(&suite->device);

    EXPECT_TRUE(pushedState.transitionToMatchResult());
}

// Test: Opponent timeout means win
inline void matchOpponentTimeoutMeansWin(MatchStateTests* suite) {
    suite->player->setIsHunter(true);

    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setOpponentNeverPressed();

    EXPECT_TRUE(suite->matchManager->didWin());
}

// ============================================
// Scenario 5: DUT never presses, opponent does
// ============================================

// Test: Grace period expires sets never pressed
inline void matchGracePeriodExpiresSetsNeverPressed(MatchStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    MatchReceivedResult receivedState(suite->ctx);
    receivedState.onStateMounted(&suite->device);
    
    // Advance past grace period (750ms)
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    // Should have set pity time and transitioned
    EXPECT_TRUE(receivedState.transitionToMatchResult());
}

// Test: Grace period expiry sends pity time
inline void matchGracePeriodExpiresSendsPityTime(MatchStateTests* suite) {
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillRepeatedly(Return(1));

    MatchReceivedResult receivedState(suite->ctx);
    receivedState.onStateMounted(&suite->device);
    
    // Advance past grace period
    suite->fakeClock->advance(800);
    receivedState.onStateLoop(&suite->device);
    
    // Hunter draw time should now be set (pity time)
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_GT(suite->matchManager->getCurrentMatch()->getHunterDrawTime(), 0);
}

// Test: Never pressed (pity time) means lose
inline void matchNeverPressedMeansLose(MatchStateTests* suite) {
    suite->player->setIsHunter(true);
    
    // Scenario: Hunter never pressed, bounty pressed quickly
    // Hunter gets pity time (e.g. 800ms) when grace period expires
    // Bounty pressed at 150ms
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setHunterDrawTime(800); // Pity time - much slower
    
    // Hunter loses because 800ms > 150ms
    EXPECT_FALSE(suite->matchManager->didWin());
}

// ============================================
// Match Result State Tests
// ============================================

class MatchResultTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        stubFixtureRdcDirectPeer(device);
        matchManager = new MatchManager(player, &storage, device.wirelessManager,
                                        &device.fakeRemoteDeviceCoordinator);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(storage, write(_, _, _)).WillByDefault(Return(100));
        ON_CALL(storage, writeUChar(_, _, _)).WillByDefault(Return(1));
        ON_CALL(storage, readUChar(_, _, _)).WillByDefault(Return(0));

        ctx.player = player;
        ctx.matchManager = matchManager;
        ctx.remoteDeviceCoordinator = &device.fakeRemoteDeviceCoordinator;
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    Player* player;
    MatchManager* matchManager;
    GameContext ctx;
    FakePlatformClock* fakeClock;
};

// Test: Hunter wins with faster time
inline void resultHunterWinsWithFasterTime(MatchResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Bounty wins with faster time
inline void resultBountyWinsWithFasterTime(MatchResultTests* suite) {
    suite->player->setIsHunter(false);
    // Simulate hunter sending SEND_MATCH_ID; this bounty receives and creates the match.
    // Use the fixture's stubbed RDC peer MAC so the SEND_MATCH_ID gate accepts it.
    uint8_t hunterMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    MatchCommand sendMatchCmd(hunterMac, MatchCmd::SEND_MATCH_ID, "test-match-id-000000000000000",
                              0, true);
    suite->matchManager->listenForMatchEvents(sendMatchCmd);
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Tied times favor opponent (local player loses)
inline void resultTiedTimesFavorOpponent(MatchResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(250);
    suite->matchManager->setBountyDrawTime(250);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    
    // With equal times, hunter_time < bounty_time is false, so hunter loses
    EXPECT_FALSE(suite->matchManager->didWin());
}

// Test: Opponent timeout means auto win
inline void resultOpponentTimeoutMeansAutoWin(MatchResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setOpponentNeverPressed();

    EXPECT_TRUE(suite->matchManager->didWin());
}

// Test: Win transitions to Win state
inline void resultWinTransitionsToWinState(MatchResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    MatchResult resultState(suite->ctx);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_TRUE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

// Test: Lose transitions to Lose state
inline void resultLoseTransitionsToLoseState(MatchResultTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setBountyDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    MatchResult resultState(suite->ctx);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_TRUE(resultState.transitionToLose());
}

// Test: Player stats updated on win
inline void resultPlayerStatsUpdatedOnWin(MatchResultTests* suite) {
    suite->player->setIsHunter(true);
    
    int initialWins = suite->player->getWins();
    int initialStreak = suite->player->getStreak();
    int initialMatches = suite->player->getMatchesPlayed();
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    MatchResult resultState(suite->ctx);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_EQ(suite->player->getWins(), initialWins + 1);
    EXPECT_EQ(suite->player->getStreak(), initialStreak + 1);
    EXPECT_EQ(suite->player->getMatchesPlayed(), initialMatches + 1);
}

// Test: Player stats updated on loss
inline void resultPlayerStatsUpdatedOnLoss(MatchResultTests* suite) {
    suite->player->setIsHunter(true);
    
    // Set up an initial streak
    suite->player->incrementWins();
    suite->player->incrementStreak();
    suite->player->incrementStreak();
    
    int initialLosses = suite->player->getLosses();
    int initialMatches = suite->player->getMatchesPlayed();
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(400);
    suite->matchManager->setBountyDrawTime(200);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    MatchResult resultState(suite->ctx);
    resultState.onStateMounted(&suite->device);
    
    EXPECT_EQ(suite->player->getLosses(), initialLosses + 1);
    EXPECT_EQ(suite->player->getStreak(), 0); // Streak reset
    EXPECT_EQ(suite->player->getMatchesPlayed(), initialMatches + 1);
}

// Test: Match finalized on result
inline void resultMatchFinalizedOnResult(MatchResultTests* suite) {
    UUIDBytes hunterId{};
    UUIDBytes::parse("0a0a0a0a-0000-4000-8000-000000000003", hunterId);
    Team hunterTeam{};
    strncpy(hunterTeam.name, "Hunter", sizeof(hunterTeam.name) - 1);
    suite->player->applyRegistrationProfile(hunterId, "LocalHunter", hunterTeam, {});

    EXPECT_CALL(suite->storage, writeBytes(MATCHES_PREFS_NAMESPACE, _, _, MATCH_BINARY_SIZE))
    .Times(testing::AtLeast(1))
    .WillRepeatedly(Return(MATCH_BINARY_SIZE));
    EXPECT_CALL(suite->storage, writeUInt(MATCHES_PREFS_NAMESPACE, PREF_SEQ_KEY, _))
    .Times(testing::AtLeast(1))
    .WillRepeatedly(Return(sizeof(uint32_t)));
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    UUIDBytes bountyId{};
    UUIDBytes::parse("0b0b0b0b-0000-4000-8000-000000000004", bountyId);
    suite->matchManager->getCurrentMatch()->setBountyPlayerId(bountyId);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    MatchResult resultState(suite->ctx);
    resultState.onStateMounted(&suite->device);
    
    // Match should be finalized (saved to storage)
    // We verify this by checking the mock was called
    SUCCEED();
}

// The ordinary press-and-lose path, with no timeout involved. The winner's
// MATCH_RESULT lands while this device's match is still running — on hardware
// that is the whole race — and MatchResult mounts after it. Resolving the outcome
// from the standing role there put the loser on the winner branch, so one match
// produced two eliminations and a two-player final could eliminate both
// finalists.
inline void resultShootoutLoserDoesNotClaimTheWin(MatchResultTests* suite) {
    uint8_t selfMac[6] = {0x05, 0, 0, 0, 0, 0};
    ShootoutManager shootout(suite->player, suite->device.wirelessManager,
                             &suite->device.fakeRemoteDeviceCoordinator, suite->matchManager);
    std::array<uint8_t, 6> opponent =
    mountShootoutMatchWithBountySlot(shootout, suite->device, selfMac);
    suite->ctx.shootoutManager = &shootout;
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    ASSERT_TRUE(suite->player->isHunter()) << "the standing role should be the opposite";
    ASSERT_FALSE(suite->matchManager->isLocalHunter());

    // Both draw times in: this device, in the bounty slot, was slower.
    suite->matchManager->setBountyDrawTime(400);
    suite->matchManager->setHunterDrawTime(100);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    shootout.onMatchResultReceived(opponent.data(), selfMac, 0, 9, opponent.data());
    ASSERT_EQ(shootout.getPhase(), ShootoutManager::Phase::BETWEEN_MATCHES);

    MatchResult resultState(suite->ctx);
    resultState.onStateMounted(&suite->device);

    EXPECT_FALSE(resultState.transitionToShootoutSpectator())
        << "the loser took the winner branch";
    EXPECT_TRUE(resultState.transitionToShootoutEliminated());
    EXPECT_FALSE(shootout.isEliminated(opponent.data()))
        << "one match produced two eliminations";

    suite->ctx.shootoutManager = nullptr;
}

// ============================================
// State Cleanup Verification Tests
// ============================================

class StateCleanupTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        player = new Player();
        { char pid[] = "1234"; player->setUserID(pid); }
        player->setIsHunter(true);

        stubFixtureRdcDirectPeer(device);
        matchManager = new MatchManager(player, &storage, device.wirelessManager,
                                        &device.fakeRemoteDeviceCoordinator);

        posseManager = new PosseManager(player, device.wirelessManager, &device.fakeRemoteDeviceCoordinator);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(storage, write(_, _, _)).WillByDefault(Return(100));
        ON_CALL(storage, writeUChar(_, _, _)).WillByDefault(Return(1));
        ON_CALL(storage, readUChar(_, _, _)).WillByDefault(Return(0));

        ctx.player = player;
        ctx.matchManager = matchManager;
        ctx.remoteDeviceCoordinator = &device.fakeRemoteDeviceCoordinator;
        ctx.posseManager = posseManager;
    }

    void TearDown() override {
        matchManager->clearCurrentMatch();
        delete posseManager;
        delete matchManager;
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    MockPeerComms peerComms;
    MockStorage storage;
    Player* player;
    MatchManager* matchManager;
    PosseManager* posseManager;
    GameContext ctx;
    FakePlatformClock* fakeClock;
};

// Test: Idle state clears button callbacks on dismount
inline void cleanupIdleClearsButtonCallbacks(StateCleanupTests* suite) {
    Idle idleState(suite->ctx);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);

    idleState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    idleState.onStateDismounted(&suite->device);
}

// Test: Countdown state clears button callbacks on dismount
inline void cleanupCountdownClearsButtonCallbacks(StateCleanupTests* suite) {
    MatchCountdown countdownState(suite->ctx);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    countdownState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    countdownState.onStateDismounted(&suite->device);
}

// The match continues into MatchPushed, so the match has to survive the dismount —
// that state reads it and writes the draw result against it.
// Leaving the match app is what ends a match, so MatchApp owns the teardown. The
// states inside it hand the match to one another and must not tear it down: which
// edge the machine takes is decided by registration order in populateStateMap, so
// no state can tell from its own flags whether it is handing off or leaving.
inline void matchAppDismountEndsTheMatch(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    MatchApp app(suite->ctx);
    app.populateStateMap();

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
        .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
        .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks())
        .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks())
        .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    app.onStateMounted(&suite->device);

    EXPECT_CALL(*suite->device.mockHaptics, off()).Times(testing::AtLeast(1));
    app.onStateDismounted(&suite->device);

    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value())
    << "a match left ready bounces the device into a phantom match from Idle";
}

// The other half of the same rule: a state dismounting on its own leaves the match
// alone, because it may be handing off to a sibling that needs it. Pinned on MatchState,
// whose exits used to be enumerated by flag — the case that broke was an abort
// winning while the press flag was set.
inline void aMatchStateDismountLeavesTheMatchToTheApp(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    MatchState matchState(suite->ctx);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    matchState.onStateMounted(&suite->device);

    suite->fakeClock->advance(200);
    suite->matchManager->getMatchButtonPush()(suite->matchManager);
    matchState.onStateLoop(&suite->device);
    ASSERT_TRUE(matchState.transitionToMatchPushed());

    matchState.onStateDismounted(&suite->device);
    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value())
    << "the match was torn down on the way into MatchPushed";
}

// Test: MatchReceivedResult state clears button callbacks on dismount
inline void cleanupMatchReceivedResultClearsButtonCallbacks(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    MatchReceivedResult receivedState(suite->ctx);
    receivedState.onStateMounted(&suite->device);
    
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    
    receivedState.onStateDismounted(&suite->device);
}

// Test: MatchState invalidates timer on dismount
inline void cleanupMatchStateInvalidatesTimer(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    // Player is hunter, so OUTPUT_JACK must be connected for isConnected() to return true.
    // Without this, onStateLoop would transition to idle due to disconnection, not the timer.
    suite->device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);

    MatchState matchState(suite->ctx);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    mountState(&matchState, &suite->device);

    // Advance time but not enough to timeout
    suite->fakeClock->advance(2000);
    matchState.onStateLoop(&suite->device);

    // Should not timeout yet
    EXPECT_FALSE(matchState.transitionToIdle());

    // Dismount (timer should be invalidated)
    dismountState(&matchState, &suite->device);

    // Transition flags should be reset
    EXPECT_FALSE(matchState.transitionToIdle());
    EXPECT_FALSE(matchState.transitionToMatchPushed());
    EXPECT_FALSE(matchState.transitionToMatchReceivedResult());
}

// The jack edge reaches a mounted MatchState: it stays in the draw window while
// cabled and leaves on the tick after the cable comes out.
inline void matchStateLeavesOnTheDisconnectEdge(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    MatchState matchState(suite->ctx);
    mountState(&matchState, &suite->device);
    suite->fakeClock->advance(1000);
    matchState.onStateLoop(&suite->device);
    ASSERT_FALSE(matchState.transitionToIdle()) << "left with the cable still in";

    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    matchState.onStateLoop(&suite->device);
    EXPECT_TRUE(matchState.transitionToIdle());

    dismountState(&matchState, &suite->device);
}

// Test: Countdown state invalidates timer on dismount
inline void cleanupCountdownStateInvalidatesTimer(StateCleanupTests* suite) {
    MatchCountdown countdownState(suite->ctx);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    
    countdownState.onStateMounted(&suite->device);
    
    // Advance through some stages
    suite->fakeClock->advance(2100);
    countdownState.onStateLoop(&suite->device);
    
    // Dismount
    countdownState.onStateDismounted(&suite->device);
    
    // Battle flag should be reset
    EXPECT_FALSE(countdownState.shallWeBattle());
}

inline void cleanupMatchResultResetsStateOnDismount(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setHunterDrawTime(200);
    suite->matchManager->setBountyDrawTime(300);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();

    MatchResult resultState(suite->ctx);
    resultState.onStateMounted(&suite->device);
    
    // Dismount should clear state
    resultState.onStateDismounted(&suite->device);
    
    // Transition flags should be reset
    EXPECT_FALSE(resultState.transitionToWin());
    EXPECT_FALSE(resultState.transitionToLose());
}

// Sleep follows Win and Lose and never touches the LEDs, so a looping result
// animation left running would play through the whole sleep.
inline void resultStatesStopTheirAnimationOnDismount(StateCleanupTests* suite) {
    Win win(suite->ctx);
    win.onStateMounted(&suite->device);
    ASSERT_TRUE(suite->device.getLightManager()->isAnimating());
    win.onStateDismounted(&suite->device);
    EXPECT_FALSE(suite->device.getLightManager()->isAnimating());

    Lose lose(suite->ctx);
    lose.onStateMounted(&suite->device);
    ASSERT_TRUE(suite->device.getLightManager()->isAnimating());
    lose.onStateDismounted(&suite->device);
    EXPECT_FALSE(suite->device.getLightManager()->isAnimating());
}

// Test: Match manager clears current match properly
inline void cleanupMatchManagerClearsCurrentMatch(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    
    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());
    
    suite->matchManager->clearCurrentMatch();
    
    EXPECT_FALSE(suite->matchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
}

// Test: Match manager clears match state on match clear
inline void cleanupMatchManagerClearsMatchState(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    // Set up match state
    suite->matchManager->setMatchLocalStartTime(5000);
    suite->matchManager->setReceivedButtonPush();
    suite->matchManager->setReceivedDrawResult();
    suite->matchManager->setHunterDrawTime(200);
    
    // Clear match
    suite->matchManager->clearCurrentMatch();

    // All match state should be reset
    EXPECT_EQ(suite->matchManager->getMatchLocalStartTime(), 0);
    EXPECT_FALSE(suite->matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(suite->matchManager->getHasPressedButton());
}

// ============================================
// MatchState Callback Cleanup (new behaviours from refactor)
// ============================================

// Test: MatchState removes button callbacks when transitioning to MatchReceivedResult
inline void
cleanupMatchStateClearsCallbacksWhenGoingToMatchReceivedResult(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);

    MatchState matchState(suite->ctx);

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    matchState.onStateMounted(&suite->device);

    // Simulate receiving opponent's result so transitionToMatchReceivedResult fires
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();
    matchState.onStateLoop(&suite->device);
    ASSERT_TRUE(matchState.transitionToMatchReceivedResult());

    // MatchState must now remove callbacks on dismount so MatchReceivedResult starts clean
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(1);
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(1);

    matchState.onStateDismounted(&suite->device);
}

// A disconnect ends the match through the app's abandon edge, not through the
// state. MatchPushed leaves the match alone even while disconnected, because the
// same dismount also runs on the way to MatchResult — which scores the match off
// that match, and used to be handed an empty one on this exact path.
inline void pushedLeavesTheMatchAloneWhenDisconnected(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setReceivedButtonPush();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    // FakeRemoteDeviceCoordinator reports DISCONNECTED, so isConnected() == false
    MatchPushed pushedState(suite->ctx);
    pushedState.onStateMounted(&suite->device);
    pushedState.onStateDismounted(&suite->device);

    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value())
        << "the state cleared a match the app had not finished with";
}

// A one-tick cable blip must not abort a match: in a shootout it orphans the
// player and the spectators wait forever for a MATCH_RESULT.
inline void expectDisconnectDebounce(StateCleanupTests* suite, ConnectState<PDN>& state) {
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks())
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks())
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    mountState(&state, &suite->device);

    // Cabled for far longer than the window. A state that had not heard the
    // connect would call this a persistent loss.
    suite->fakeClock->advance(2000);
    EXPECT_FALSE(matchReturnsToIdle(state, suite->ctx.shootoutManager));

    // Single-tick blip: absorbed by the debounce.
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    EXPECT_FALSE(matchReturnsToIdle(state, suite->ctx.shootoutManager));

    // Recovery within the window closes it for good.
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    suite->fakeClock->advance(2000);
    EXPECT_FALSE(matchReturnsToIdle(state, suite->ctx.shootoutManager));

    // The window opens on the edge, not on the first tick that asks after it.
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    suite->fakeClock->advance(2000);
    EXPECT_TRUE(matchReturnsToIdle(state, suite->ctx.shootoutManager));

    dismountState(&state, &suite->device);
}

inline void countdownDebouncesTransientDisconnect(StateCleanupTests* suite) {
    MatchCountdown countdown(suite->ctx);
    expectDisconnectDebounce(suite, countdown);
}

// Same debounce contract on MatchPushed (between BATTLE press and result
// arrival). A blip here threw away the match before the grace period could
// resolve it.
inline void matchPushedDebouncesTransientDisconnect(StateCleanupTests* suite) {
    MatchPushed pushed(suite->ctx);
    expectDisconnectDebounce(suite, pushed);
}

// Same debounce contract on MatchReceivedResult.
inline void matchReceivedResultDebouncesTransientDisconnect(StateCleanupTests* suite) {
    MatchReceivedResult received(suite->ctx);
    expectDisconnectDebounce(suite, received);
}

// A window left open by one tenure must not be read by the next: the state
// object outlives its mounts, and an old window would read as long expired.
inline void disconnectWindowDoesNotCarryIntoTheNextTenure(StateCleanupTests* suite) {
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks())
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks())
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());

    MatchPushed pushed(suite->ctx);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    mountState(&pushed, &suite->device);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    ASSERT_FALSE(matchReturnsToIdle(pushed, suite->ctx.shootoutManager));
    dismountState(&pushed, &suite->device);

    // Long after, mounted again with nothing cabled: the window starts over at
    // this mount.
    suite->fakeClock->advance(60000);
    mountState(&pushed, &suite->device);
    EXPECT_FALSE(matchReturnsToIdle(pushed, suite->ctx.shootoutManager));
    suite->fakeClock->advance(2000);
    EXPECT_TRUE(matchReturnsToIdle(pushed, suite->ctx.shootoutManager));

    dismountState(&pushed, &suite->device);
}

// Regression: while a shootout is live, matchReturnsToIdle must suppress the
// idle-return AND reset the disconnect debounce. The debounce ages on wall
// clock even when unsampled, so a run started before or during the tournament
// would otherwise fire the instant the shootout ends; a fresh full window
// must be required instead.
inline void countdownFreezesDisconnectDebounceDuringShootout(StateCleanupTests* suite) {
    ShootoutManager shootout(suite->player, suite->device.wirelessManager,
                             &suite->device.fakeRemoteDeviceCoordinator, nullptr);
    shootout.startProposal();
    ASSERT_TRUE(shootout.active());
    suite->ctx.shootoutManager = &shootout;

    suite->device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    MatchCountdown countdown(suite->ctx);
    mountState(&countdown, &suite->device);

    // Persistent loss during the tournament: suppressed, and the debounce must
    // not age even as wall-clock time passes.
    suite->device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    EXPECT_FALSE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));
    suite->fakeClock->advance(2000);
    EXPECT_FALSE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));

    // Tournament ends. One tick does not bail; a fresh full window is still
    // required.
    shootout.resetToIdle();
    ASSERT_FALSE(shootout.active());
    EXPECT_FALSE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));
    suite->fakeClock->advance(2000);
    EXPECT_TRUE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));

    // Ordering 2: the debounce starts BEFORE the tournament activates and
    // expires on wall clock while it is live. Without the in-flight reset the
    // first sample after the shootout ends snaps straight to Idle.
    suite->device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    EXPECT_FALSE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));
    suite->device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    EXPECT_FALSE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));  // start debounce
    suite->fakeClock->advance(2000);

    shootout.startProposal();
    ASSERT_TRUE(shootout.active());
    EXPECT_FALSE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));

    shootout.resetToIdle();
    ASSERT_FALSE(shootout.active());
    EXPECT_FALSE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));
    suite->fakeClock->advance(2000);
    EXPECT_TRUE(matchReturnsToIdle(countdown, suite->ctx.shootoutManager));

    dismountState(&countdown, &suite->device);
    suite->ctx.shootoutManager = nullptr;
}

inline void receivedResultLeavesTheMatchAloneWhenDisconnected(StateCleanupTests* suite) {
    uint8_t dummyMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    suite->matchManager->initializeMatch(dummyMac);
    suite->matchManager->setBountyDrawTime(150);
    suite->matchManager->setReceivedDrawResult();

    ASSERT_TRUE(suite->matchManager->getCurrentMatch().has_value());

    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks()).Times(testing::AnyNumber());

    MatchReceivedResult receivedState(suite->ctx);
    receivedState.onStateMounted(&suite->device);
    receivedState.onStateDismounted(&suite->device);

    EXPECT_TRUE(suite->matchManager->getCurrentMatch().has_value())
        << "the state cleared a match the app had not finished with";
}

inline void idleEntersSymbolOnlyFromTheOutputJack(StateCleanupTests* suite) {
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    EXPECT_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPrimaryButton, removeButtonCallbacks())
    .Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockSecondaryButton, removeButtonCallbacks())
    .Times(testing::AnyNumber());
    Idle idleState(suite->ctx);
    mountState(&idleState, &suite->device);

    rdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::FDN);
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    EXPECT_FALSE(idleState.transitionToSymbol())
    << "SymbolState only talks to an FDN on the output jack; entering from the input jack "
       "bounces straight back to Idle";

    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    EXPECT_TRUE(idleState.transitionToSymbol());

    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    EXPECT_FALSE(idleState.transitionToSymbol()) << "the FDN was uncabled";

    // An FDN cabled while Idle was not mounted is replayed to it at mount.
    dismountState(&idleState, &suite->device);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    EXPECT_FALSE(idleState.transitionToSymbol()) << "an unmounted state heard a jack edge";
    mountState(&idleState, &suite->device);
    EXPECT_TRUE(idleState.transitionToSymbol());

    dismountState(&idleState, &suite->device);
}

// A dismount that waits on the platform clock never returns under the fake clock, so in this
// test and the next, returning is the assertion.
inline void symbolStateLeavesForIdleWithoutBlocking(StateCleanupTests* suite) {
    SymbolWirelessManager symbolWireless(suite->device.wirelessManager,
                                         &suite->device.fakeRemoteDeviceCoordinator);
    suite->ctx.symbolWirelessManager = &symbolWireless;
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    uint8_t fdnMac[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, fdnMac);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    EXPECT_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .Times(testing::AnyNumber());

    SymbolState symbolState(suite->ctx);
    mountState(&symbolState, &suite->device);
    // The loop does not look at the cable until the half-second loading
    // animation has run.
    suite->fakeClock->advance(501);
    symbolState.onStateLoop(&suite->device);
    ASSERT_FALSE(symbolState.transitionToIdle()) << "left with the FDN still cabled";

    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    symbolState.onStateLoop(&suite->device);
    ASSERT_TRUE(symbolState.transitionToIdle());

    dismountState(&symbolState, &suite->device);
}

inline void symbolMatchedLeavesForIdleWithoutBlocking(StateCleanupTests* suite) {
    SymbolWirelessManager symbolWireless(suite->device.wirelessManager,
                                         &suite->device.fakeRemoteDeviceCoordinator);
    suite->ctx.symbolWirelessManager = &symbolWireless;
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    EXPECT_CALL(*suite->device.mockDisplay, whiteScreen())
    .WillRepeatedly(Return(suite->device.mockDisplay));
    EXPECT_CALL(*suite->device.mockHaptics, setIntensity(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockHaptics, off()).Times(testing::AnyNumber());

    SymbolMatched symbolMatched(suite->ctx);
    mountState(&symbolMatched, &suite->device);
    symbolMatched.onStateLoop(&suite->device);
    ASSERT_FALSE(symbolMatched.transitionToIdle()) << "left with the FDN still cabled";

    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    symbolMatched.onStateLoop(&suite->device);
    ASSERT_TRUE(symbolMatched.transitionToIdle());

    dismountState(&symbolMatched, &suite->device);
}

// ============================================
// GameSession lifecycle — exercises ctor + dtor so the native_asan env
// catches leaks in the managers the session owns (matchManager,
// posseManager, shootoutManager).
// ============================================

class GameSessionLifecycleTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(mac));
        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kChainGameEvent), _, _))
            .WillByDefault(testing::DoAll(
                testing::SaveArg<1>(&chainGameEventHandler),
                testing::SaveArg<2>(&chainGameEventCtx)));

        // Mounting a real state renders, and the display API chains, so an
        // un-stubbed call returns null and the next link dereferences it.
        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawImage(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));

        player = new Player();
        char playerId[] = "life";
        player->setUserID(playerId);
    }

    void TearDown() override {
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    MockDevice device;
    FakePlatformClock* fakeClock;
    Player* player;
    uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    PeerCommsInterface::PacketCallback chainGameEventHandler;
    void* chainGameEventCtx = nullptr;
};

// Create + destroy many GameSession instances; under ASAN (env:native_asan) a
// leak in the session's ownership of matchManager / posseManager would be
// reported. Without ASAN this still catches crashes in the lifecycle path.
// The iteration between destructions is the check on the device-held slots: the tick
// callback captures the session and the device outlives it, so a slot the
// destructor failed to empty is called here with a freed `this`.
inline void gameSessionCtorDtorDoesNotLeak(GameSessionLifecycleTests* suite) {
    for (int i = 0; i < 5; i++) {
        GameSession* session = new GameSession(suite->player, &suite->device, nullptr);
        delete session;
        suite->device.loop();
    }
}

// The supporter-side COUNTDOWN reset, driven the way the radio drives it. Its
// sibling in posse-manager-tests calls the manager directly, so it stays
// green even when nothing routes the packet there; this one fails instead.
inline void gameSessionCountdownVoidsStandingConfirm(GameSessionLifecycleTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t challenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

    // A hunter's opponent jack is OUTPUT, and only an opponent-jack announce
    // caches challengerMac — which is in turn what admits the game event below.
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, challenger);
    rdc.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);

    // The role cascade emits its own traffic; a catch-all keeps that off the
    // confirm counter below.
    int confirmsSent = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kChainConfirm, _, _))
        .WillRepeatedly([&confirmsSent](const uint8_t*, PktType, const uint8_t*, const size_t) {
            confirmsSent++;
            return 1;
        });

    auto* session = new GameSession(suite->player, &suite->device, nullptr);
    PosseManager* posseManager = session->getContext().posseManager;

    posseManager->onRoleAnnounceReceived(challenger, challenger);
    ASSERT_TRUE(posseManager->isSupporter());
    posseManager->sendConfirm();
    ASSERT_EQ(confirmsSent, 1);

    ASSERT_NE(suite->chainGameEventHandler, nullptr);
    ChainGameEventPayload countdown{};
    countdown.event_type = static_cast<uint8_t>(ChainGameEventType::COUNTDOWN);
    countdown.seqId = 0;
    // The frame is broadcast and the challenger it names is what admits it, so an
    // unset challengerMac here would be dropped before the reset under test runs.
    memcpy(countdown.challengerMac, challenger, 6);
    suite->chainGameEventHandler(challenger, reinterpret_cast<const uint8_t*>(&countdown),
                                 sizeof(countdown), suite->chainGameEventCtx);

    // The press is spent, so the challenger change must not resurrect it.
    uint8_t secondChallenger[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    posseManager->onRoleAnnounceReceived(challenger, secondChallenger);
    EXPECT_EQ(confirmsSent, 1);

    delete session;
}

// The event's other consumer: the mounted SupporterReady, which subscribes to
// the posse manager for as long as it is mounted. Without it no supporter is
// armed, so the challenger plays without their boost.
inline void gameSessionCountdownArmsMountedSupporter(GameSessionLifecycleTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t challenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, challenger);
    rdc.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillRepeatedly(Return(1));

    GameSession* session = new GameSession(suite->player, &suite->device, nullptr);
    PosseManager* posseManager = session->getContext().posseManager;
    posseManager->onRoleAnnounceReceived(challenger, challenger);
    ASSERT_TRUE(posseManager->isSupporter());

    HubApp* hub = new HubApp(session->getContext());
    AppConfig apps;
    apps[StateId(HUB_APP_ID)] = hub;
    suite->device.loadAppConfig(apps, StateId(HUB_APP_ID));
    suite->device.setActiveApp(StateId(HUB_APP_ID), StateId(SUPPORTER_READY));
    ASSERT_EQ(suite->device.getActiveApp()->getCurrentState()->getStateId(), SUPPORTER_READY);

    ChainGameEventPayload countdown{};
    countdown.event_type = static_cast<uint8_t>(ChainGameEventType::COUNTDOWN);
    countdown.seqId = 0;
    memcpy(countdown.challengerMac, challenger, 6);
    ASSERT_NE(suite->chainGameEventHandler, nullptr);
    suite->chainGameEventHandler(challenger, reinterpret_cast<const uint8_t*>(&countdown),
                                 sizeof(countdown), suite->chainGameEventCtx);

    SupporterReady* supporterReady =
        static_cast<SupporterReady*>(suite->device.getActiveApp()->getCurrentState());
    EXPECT_TRUE(supporterReady->buttonArmed);

    delete hub;
    delete session;
}

// A result stays up for 8 s and then a supporter who pressed is back on Ready.
// Left confirmed, they would sit on "Locked In" through the next round.
inline void supporterResultClearsAfterEightSeconds(GameSessionLifecycleTests* suite) {
    suite->player->setIsHunter(true);
    uint8_t challenger[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, challenger);
    rdc.setPeerRole(SerialIdentifier::OUTPUT_JACK, true);

    GameSession* session = new GameSession(suite->player, &suite->device, nullptr);
    PosseManager* posseManager = session->getContext().posseManager;
    posseManager->onRoleAnnounceReceived(challenger, challenger);
    ASSERT_TRUE(posseManager->isSupporter());

    SupporterReady supporterReady(session->getContext());
    supporterReady.onStateMounted(&suite->device);
    supporterReady.onChainGameEventReceived(static_cast<uint8_t>(ChainGameEventType::COUNTDOWN));
    suite->device.mockPrimaryButton->trigger(ButtonInteraction::PRESS);
    ASSERT_TRUE(supporterReady.hasConfirmed);
    supporterReady.onChainGameEventReceived(static_cast<uint8_t>(ChainGameEventType::WIN));

    // The challenger retransmits a WIN it has not seen acked; the repeat must
    // not restart the eight seconds.
    suite->fakeClock->advance(700);
    supporterReady.onChainGameEventReceived(static_cast<uint8_t>(ChainGameEventType::WIN));

    suite->fakeClock->advance(7200);
    supporterReady.onStateLoop(&suite->device);
    EXPECT_EQ(supporterReady.lastResult, 1);
    EXPECT_TRUE(supporterReady.hasConfirmed);

    suite->fakeClock->advance(200);
    supporterReady.onStateLoop(&suite->device);
    EXPECT_EQ(supporterReady.lastResult, 0);
    EXPECT_FALSE(supporterReady.hasConfirmed);

    supporterReady.onStateDismounted(&suite->device);
    delete session;
}
