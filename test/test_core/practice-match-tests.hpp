#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <string>
#include <vector>

#include "connect-state-callback-tests.hpp"
#include "game/posse-manager.hpp"
#include "game/quickdraw-states.hpp"
#include "match-manager-tests.hpp"
#include "player-provider-tests.hpp"
#include "quickdraw-integration-tests.hpp"

// ============================================
// Practice matches (#498)
// ============================================
//
// A standalone match scores only when both player UUIDs are non-nil and
// distinct. Anything else is a practice match: played and shown, labelled on
// both PDNs, never stored. Each PDN decides from the identities the real
// handshake exchanged, so these run the handshake rather than priming a match.

/** Every string a mock display was asked to draw. */
class ScreenText {
public:
    explicit ScreenText(MockDevice& device) {
        MockDisplay* display = device.mockDisplay;
        ON_CALL(*display, invalidateScreen()).WillByDefault(Return(display));
        ON_CALL(*display, drawImage(_)).WillByDefault(Return(display));
        ON_CALL(*display, drawImage(_, _, _)).WillByDefault(Return(display));
        ON_CALL(*display, setGlyphMode(_)).WillByDefault(Return(display));
        ON_CALL(*display, whiteScreenLeftHalf()).WillByDefault(Return(display));
        ON_CALL(*display, renderGlyph(_, _, _)).WillByDefault(Return(display));
        ON_CALL(*display, drawText(_, _, _))
        .WillByDefault(Invoke([this, display](const char* text, int, int) {
            lines.emplace_back(text);
            return display;
        }));
    }

    bool shows(const char* text) const {
        return std::find(lines.begin(), lines.end(), text) != lines.end();
    }

    void clear() { lines.clear(); }

private:
    std::vector<std::string> lines;
};

class PracticeMatchTests : public TwoDeviceTestFixture {
public:
    void SetUp() override {
        TwoDeviceTestFixture::SetUp();
        hunterPosse = new PosseManager(hunter, hunterDeviceWirelessManager, &hunterFakeRdc);
        bountyPosse = new PosseManager(bounty, bountyDeviceWirelessManager, &bountyFakeRdc);
        hunterCtx = contextFor(hunter, hunterMatchManager, &hunterFakeRdc, hunterPosse);
        bountyCtx = contextFor(bounty, bountyMatchManager, &bountyFakeRdc, bountyPosse);
    }

    void TearDown() override {
        delete bountyPosse;
        delete hunterPosse;
        TwoDeviceTestFixture::TearDown();
    }

    /** Applies the given identities (nullptr leaves a player nil) and reruns the handshake. */
    void handshakeAs(const char* hunterPlayerId, const char* bountyPlayerId) {
        if (hunterPlayerId != nullptr) applyTestRegistration(hunter, hunterPlayerId, "Hunter");
        if (bountyPlayerId != nullptr) applyTestRegistration(bounty, bountyPlayerId, "Bounty");
        // Registration may have flipped a standing role; the match needs one of each.
        hunter->setIsHunter(true);
        bounty->setIsHunter(false);
        replayHandshake();
        ASSERT_TRUE(hunterMatchManager->isMatchReady());
        ASSERT_TRUE(bountyMatchManager->isMatchReady());
    }

    /** Mounts and dismounts the countdown on one PDN, as the state machine does. */
    void showCountdown(MockDevice& device, const GameContext& ctx) {
        MatchCountdown countdown(ctx);
        mountState(&countdown, &device);
        dismountState(&countdown, &device);
    }

    /** Both press, both results cross, and each PDN runs its result and win or lose screen. */
    void playToResultScreens() {
        fakeClock->advance(150);
        hunterMatchManager->getMatchButtonPush()(hunterMatchManager);
        fakeClock->advance(30);
        bountyMatchManager->getMatchButtonPush()(bountyMatchManager);
        hunterSendsToBounty();
        bountySendsToHunter();
        showResult(hunterDevice, hunterCtx);
        showResult(bountyDevice, bountyCtx);
    }

    MockDevice hunterDevice;
    MockDevice bountyDevice;
    ScreenText hunterScreen{hunterDevice};
    ScreenText bountyScreen{bountyDevice};
    PosseManager* hunterPosse = nullptr;
    PosseManager* bountyPosse = nullptr;
    GameContext hunterCtx;
    GameContext bountyCtx;

private:
    static GameContext contextFor(Player* player, MatchManager* matchManager,
                                  RemoteDeviceCoordinator* rdc, PosseManager* posseManager) {
        GameContext ctx;
        ctx.player = player;
        ctx.matchManager = matchManager;
        ctx.remoteDeviceCoordinator = rdc;
        ctx.posseManager = posseManager;
        return ctx;
    }

    void showResult(MockDevice& device, const GameContext& ctx) {
        MatchResult result(ctx);
        mountState(&result, &device);
        const bool won = result.transitionToWin();
        dismountState(&result, &device);
        if (won) {
            Win win(ctx);
            mountState(&win, &device);
            dismountState(&win, &device);
        } else {
            Lose lose(ctx);
            mountState(&lose, &device);
            dismountState(&lose, &device);
        }
    }
};

// Both PDNs label the countdown and the result, and neither stores the match.
inline void expectPracticeOnBothPdns(PracticeMatchTests* suite) {
    EXPECT_CALL(suite->hunterStorage, writeBytes(_, _, _, _)).Times(0);
    EXPECT_CALL(suite->bountyStorage, writeBytes(_, _, _, _)).Times(0);
    EXPECT_CALL(suite->hunterStorage, writeUInt(_, _, _)).Times(0);
    EXPECT_CALL(suite->bountyStorage, writeUInt(_, _, _)).Times(0);

    suite->showCountdown(suite->hunterDevice, suite->hunterCtx);
    suite->showCountdown(suite->bountyDevice, suite->bountyCtx);
    EXPECT_TRUE(suite->hunterScreen.shows("PRACTICE")) << "hunter countdown";
    EXPECT_TRUE(suite->hunterScreen.shows("SCORED")) << "hunter countdown";
    EXPECT_TRUE(suite->bountyScreen.shows("PRACTICE")) << "bounty countdown";
    EXPECT_TRUE(suite->bountyScreen.shows("SCORED")) << "bounty countdown";

    suite->hunterScreen.clear();
    suite->bountyScreen.clear();
    suite->playToResultScreens();
    EXPECT_TRUE(suite->hunterScreen.shows("PRACTICE")) << "hunter result";
    EXPECT_TRUE(suite->bountyScreen.shows("PRACTICE")) << "bounty result";
}

TEST_F(PracticeMatchTests, nilHunterIsPracticeOnBothPdns) {
    handshakeAs(nullptr, OPPONENT_PLAYER_UUID);
    expectPracticeOnBothPdns(this);
}

TEST_F(PracticeMatchTests, nilBountyIsPracticeOnBothPdns) {
    handshakeAs(LOCAL_PLAYER_UUID, nullptr);
    expectPracticeOnBothPdns(this);
}

TEST_F(PracticeMatchTests, samePlayerOnBothPdnsIsPractice) {
    handshakeAs(LOCAL_PLAYER_UUID, LOCAL_PLAYER_UUID);
    expectPracticeOnBothPdns(this);
}

TEST_F(PracticeMatchTests, distinctPlayersScoreWithoutALabel) {
    handshakeAs(LOCAL_PLAYER_UUID, OPPONENT_PLAYER_UUID);
    EXPECT_CALL(hunterStorage, writeBytes(MATCH_RING_PREFS_NAMESPACE, _, _, _)).Times(1);
    EXPECT_CALL(bountyStorage, writeBytes(MATCH_RING_PREFS_NAMESPACE, _, _, _)).Times(1);

    showCountdown(hunterDevice, hunterCtx);
    showCountdown(bountyDevice, bountyCtx);
    playToResultScreens();

    EXPECT_FALSE(hunterScreen.shows("PRACTICE"));
    EXPECT_FALSE(bountyScreen.shows("PRACTICE"));
}

// The tournament scores a shootout match. Its opponent slot is never filled by
// a handshake, so a label read off the UUIDs alone would wrongly show here.
TEST_F(PracticeMatchTests, shootoutMatchShowsNoLabel) {
    applyTestRegistration(hunter, LOCAL_PLAYER_UUID, "Hunter");
    hunterMatchManager->clearCurrentMatch();
    uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    hunterMatchManager->initializeShootoutMatch("SHT-0001000200030004000500060007", bountyMac,
                                                true);

    showCountdown(hunterDevice, hunterCtx);

    EXPECT_FALSE(hunterScreen.shows("PRACTICE"));
}

// ============================================
// Unregistered idle indicator (#498)
// ============================================

inline bool idleShowsUnregistered(PracticeMatchTests* suite) {
    suite->hunterScreen.clear();
    Idle idle(suite->hunterCtx);
    mountState(&idle, &suite->hunterDevice);
    idle.onStateLoop(&suite->hunterDevice);
    dismountState(&idle, &suite->hunterDevice);
    return suite->hunterScreen.shows("UNREGIS-") && suite->hunterScreen.shows("TERED");
}

TEST_F(PracticeMatchTests, idleLabelTracksANilPlayerUuid) {
    hunterMatchManager->clearCurrentMatch();
    EXPECT_TRUE(idleShowsUnregistered(this)) << "nil player UUID";

    applyTestRegistration(hunter, LOCAL_PLAYER_UUID, "Hunter");
    EXPECT_FALSE(idleShowsUnregistered(this)) << "registered";
    EXPECT_FALSE(hunterScreen.shows("UNREGIS-"));

    uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    hunter->applyRegistrationProfile(Player::testPlayerIdFromMac(mac), "Test", Team{}, {});
    EXPECT_FALSE(idleShowsUnregistered(this)) << "test build identity";

    hunter->clearRegistrationProfile();
    EXPECT_TRUE(idleShowsUnregistered(this)) << "registration cleared";
}

// The stats cycle steps off the label and comes back to it when it wraps.
TEST_F(PracticeMatchTests, idleLabelReturnsWhenTheStatsCycleWraps) {
    hunterMatchManager->clearCurrentMatch();
    Idle idle(hunterCtx);
    mountState(&idle, &hunterDevice);
    idle.onStateLoop(&hunterDevice);
    ASSERT_TRUE(hunterScreen.shows("UNREGIS-"));

    hunterScreen.clear();
    hunterDevice.mockPrimaryButton->trigger(ButtonInteraction::CLICK);
    idle.onStateLoop(&hunterDevice);
    EXPECT_FALSE(hunterScreen.shows("UNREGIS-")) << "first stats page";

    // Eight stats pages, 0 through 7; the click after the last wraps.
    for (int click = 0; click < 8; click++) {
        hunterDevice.mockPrimaryButton->trigger(ButtonInteraction::CLICK);
        idle.onStateLoop(&hunterDevice);
    }
    EXPECT_TRUE(hunterScreen.shows("UNREGIS-")) << "after the wrap";
    dismountState(&idle, &hunterDevice);
}
