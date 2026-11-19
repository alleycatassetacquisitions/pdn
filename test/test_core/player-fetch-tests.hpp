#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "apps/player-fetch/player-fetch.hpp"
#include "device/firmware-update-manager.hpp"
#include "fake-firmware-store.hpp"
#include "connect-state-callback-tests.hpp"
#include "utility-tests.hpp"
#include "game/player-provider.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class PlayerFetchTestSuite : public testing::Test {
public:
    /** Wires mocks and a fake platform clock for PlayerFetch state tests. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(0);

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        // Lets a test expect one specific line of text among the others a screen draws.
        EXPECT_CALL(*device.mockDisplay, drawText(_, _, _)).Times(testing::AnyNumber());
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, renderGlyph(_, _, _))
        .WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawButton(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());
    }

    /** Restores the platform clock. */
    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakePlatformClock* fakeClock = nullptr;
    PlayerProvider playerProvider;
    MockDevice device;
    /** Declared after the device and provider it borrows. */
    BandRegistration bandRegistration{device.wirelessManager, device.getRemoteDeviceCoordinator(),
                                      &playerProvider};
};

// Returns with ChooseTeam still mounted so callers can check its flags before dismounting.
inline void confirmChooseTeam(PlayerFetchTestSuite* suite, ChooseTeamState* chooseTeam,
                              uint8_t* mac, bool pickBounty) {
    parameterizedCallbackFunction primaryPress = nullptr;
    void* primaryCtx = nullptr;
    parameterizedCallbackFunction secondaryPress = nullptr;
    void* secondaryCtx = nullptr;
    ON_CALL(*suite->device.mockPeerComms, getMacAddress()).WillByDefault(Return(mac));
    ON_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
    .WillByDefault(Invoke([&](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
        primaryPress = fn;
        primaryCtx = ctx;
    }));
    ON_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .WillByDefault(Invoke([&](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
        secondaryPress = fn;
        secondaryCtx = ctx;
    }));
    mountState(chooseTeam, &suite->device);
    ASSERT_NE(primaryPress, nullptr);
    ASSERT_NE(secondaryPress, nullptr);
    if (pickBounty) {
        primaryPress(primaryCtx);
    }
    EXPECT_FALSE(chooseTeam->transitionToWelcome());
    secondaryPress(secondaryCtx);
}

inline void chooseTeamTestIdentityRegistersMacPlayerAndEntersWelcome(PlayerFetchTestSuite* suite) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x07};
    ChooseTeamState chooseTeam(&suite->playerProvider, true);
    confirmChooseTeam(suite, &chooseTeam, mac, true);

    Player* player = suite->playerProvider.getPlayer();
    EXPECT_TRUE(chooseTeam.transitionToWelcome());
    EXPECT_TRUE(player->hasRegistrationProfile());
    EXPECT_EQ(player->getServerPlayerId(), Player::testPlayerIdFromMac(mac));
    EXPECT_FALSE(player->isHunter());
    EXPECT_STREQ(player->getServerPlayerName(), "Test");
    EXPECT_FALSE(player->hasTeam());

    dismountState(&chooseTeam, &suite->device);
    EXPECT_FALSE(chooseTeam.transitionToWelcome());
}

inline void chooseTeamDemoKeepsNilPlayerId(PlayerFetchTestSuite* suite) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x07};
    ChooseTeamState chooseTeam(&suite->playerProvider, false);
    confirmChooseTeam(suite, &chooseTeam, mac, false);

    Player* player = suite->playerProvider.getPlayer();
    EXPECT_TRUE(chooseTeam.transitionToWelcome());
    EXPECT_TRUE(player->getServerPlayerId().isZero());
    EXPECT_TRUE(player->isHunter());
    EXPECT_STREQ(player->getServerPlayerName(), "Demo");

    dismountState(&chooseTeam, &suite->device);
    EXPECT_FALSE(chooseTeam.transitionToWelcome());
}

inline void awaitBandAndFirmwareUpdateLeadToEachOther(PlayerFetchTestSuite* suite) {
    FakeFirmwareStore store;
    FirmwareUpdateManager manager(suite->device.mockPeerComms, &store);
    PlayerFetchApp app(&manager, &suite->playerProvider, &suite->bandRegistration,
                       suite->device.getRemoteDeviceCoordinator(), false);
    app.populateStateMap();
    ASSERT_EQ(app.getStateMap().size(), 5u);
    State* awaitBand = app.getStateMap()[0];
    EXPECT_EQ(awaitBand->getTransitions()[2]->getNextState(), app.getStateMap()[4]);
    EXPECT_EQ(app.getStateMap()[4]->getTransitions()[0]->getNextState(), awaitBand);
}

inline void welcomeShowsPlayerNameThenHandsOff(PlayerFetchTestSuite* suite) {
    suite->playerProvider.getPlayer()->applyRegistrationProfile(UUIDBytes(), "Demo", Team(), {});
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Welcome"), _, _));
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Demo"), _, _));
    PlayerFetchWelcome welcome(&suite->playerProvider);
    mountState(&welcome, &suite->device);
    suite->fakeClock->advance(WELCOME_MESSAGE_TIMEOUT_MS);
    welcome.onStateLoop(&suite->device);
    EXPECT_FALSE(welcome.transitionToAwaken());
    suite->fakeClock->advance(1);
    welcome.onStateLoop(&suite->device);
    EXPECT_TRUE(welcome.transitionToAwaken());
    dismountState(&welcome, &suite->device);
    EXPECT_FALSE(welcome.transitionToAwaken());
}

inline void mountFetchUser(PlayerFetchTestSuite* suite, FetchUserState* fetch) {
    mountState(fetch, &suite->device);
}

inline void fetchUserFoundPlayerLeadsToWelcome(PlayerFetchTestSuite* suite) {
    Player ada;
    ada.applyRegistrationProfile(UUIDBytes(), "Ada", Team(), {});
    FetchUserState fetch(&suite->playerProvider, &suite->bandRegistration);
    mountFetchUser(suite, &fetch);
    EXPECT_FALSE(fetch.transitionToWelcome());
    fetch.onPlayerFound(ada);
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Ada");
    EXPECT_TRUE(fetch.transitionToWelcome());
    fetch.onPlayerLookupFailed();
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_FALSE(fetch.transitionToAwaitBand()) << "a failure after the answer is ignored";
    dismountState(&fetch, &suite->device);
    EXPECT_FALSE(fetch.transitionToWelcome());
    suite->playerProvider.clear();
    fetch.onPlayerFound(ada);
    EXPECT_FALSE(fetch.transitionToWelcome()) << "no lookup is outstanding after dismount";
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "an ignored answer writes nothing";
}

// Shows "Unable to / Locate Asset" for the failure-page time, then returns to the waiting screen.
inline void expectFailurePageThenAwaitBand(PlayerFetchTestSuite* suite, FetchUserState* fetch) {
    fetch->onStateLoop(&suite->device);
    EXPECT_FALSE(fetch->transitionToAwaitBand());
    fetch->onPlayerFound(Player());
    EXPECT_FALSE(fetch->transitionToWelcome()) << "a late answer is ignored";
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS);
    fetch->onStateLoop(&suite->device);
    EXPECT_FALSE(fetch->transitionToAwaitBand());
    suite->fakeClock->advance(1);
    fetch->onStateLoop(&suite->device);
    EXPECT_TRUE(fetch->transitionToAwaitBand());
    dismountState(fetch, &suite->device);
    EXPECT_FALSE(fetch->transitionToAwaitBand());
}

inline void fetchUserFailedLookupShowsFailurePage(PlayerFetchTestSuite* suite) {
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Unable to"), _, _));
    FetchUserState fetch(&suite->playerProvider, &suite->bandRegistration);
    mountFetchUser(suite, &fetch);
    fetch.onPlayerLookupFailed();
    expectFailurePageThenAwaitBand(suite, &fetch);
}

// The 10 s without the pair ends on the failure page.
inline void fetchUserTimeoutShowsFailurePage(PlayerFetchTestSuite* suite) {
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Unable to"), _, _));
    FetchUserState fetch(&suite->playerProvider, &suite->bandRegistration);
    mountFetchUser(suite, &fetch);
    suite->fakeClock->advance(USER_DATA_FETCH_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    expectFailurePageThenAwaitBand(suite, &fetch);
}
