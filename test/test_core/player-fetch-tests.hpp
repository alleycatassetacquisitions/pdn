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
};

// Until an FDN looks a PIN up (#485), every completed PIN, demo or not, leads to ChooseTeam.
inline void pinEntryRoutesPinToChooseTeam(PlayerFetchTestSuite* suite, const int digits[4],
                                          const char* pin) {
    parameterizedCallbackFunction primaryPress = nullptr;
    parameterizedCallbackFunction secondaryPress = nullptr;
    void* ctx = nullptr;
    ON_CALL(*suite->device.mockPrimaryButton, setButtonPress(_, _, _))
    .WillByDefault(
    Invoke([&](parameterizedCallbackFunction fn, void* buttonCtx, ButtonInteraction) {
        primaryPress = fn;
        ctx = buttonCtx;
    }));
    ON_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .WillByDefault(
    Invoke([&](parameterizedCallbackFunction fn, void* buttonCtx, ButtonInteraction) {
        secondaryPress = fn;
        ctx = buttonCtx;
    }));
    PinEntryState pinEntry(&suite->playerProvider, nullptr);
    mountState(&pinEntry, &suite->device);
    ASSERT_NE(secondaryPress, nullptr);
    for (int index = 0; index < 4; index++) {
        for (int press = 0; press < digits[index]; press++) {
            primaryPress(ctx);
        }
        EXPECT_FALSE(pinEntry.transitionToChooseTeam());
        secondaryPress(ctx);
    }
    EXPECT_STREQ(suite->playerProvider.getRegistrationPin(), pin);
    EXPECT_TRUE(pinEntry.transitionToChooseTeam());
    dismountState(&pinEntry, &suite->device);
    EXPECT_FALSE(pinEntry.transitionToChooseTeam());
}

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

inline void chooseTeamTestIdentityRegistersMacPlayerAndEntersHub(PlayerFetchTestSuite* suite) {
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

inline void pinEntryAndFirmwareUpdateLeadToEachOther(PlayerFetchTestSuite* suite) {
    FakeFirmwareStore store;
    FirmwareUpdateManager manager(suite->device.mockPeerComms, &store);
    PlayerFetchApp app(&manager, &suite->playerProvider, false);
    app.populateStateMap();
    ASSERT_EQ(app.getStateMap().size(), 4u);
    State* pinEntry = app.getStateMap()[0];
    EXPECT_EQ(pinEntry->getTransitions()[1]->getNextState(), app.getStateMap()[3]);
    EXPECT_EQ(app.getStateMap()[3]->getTransitions()[0]->getNextState(), pinEntry);
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

// Mounted with the PIN stored, as PIN entry leaves it; the FDN answer events come from #485.
inline void mountFetchUser(PlayerFetchTestSuite* suite, FetchUserState* fetch) {
    suite->playerProvider.setRegistrationPin("0007");
    mountState(fetch, &suite->device);
}

inline void fetchUserFoundPlayerLeadsToWelcome(PlayerFetchTestSuite* suite) {
    FetchUserState fetch(&suite->playerProvider);
    mountFetchUser(suite, &fetch);
    EXPECT_FALSE(fetch.transitionToWelcome());
    fetch.onPlayerFound();
    EXPECT_TRUE(fetch.transitionToWelcome());
    EXPECT_FALSE(fetch.transitionToPinEntry());
    dismountState(&fetch, &suite->device);
    EXPECT_FALSE(fetch.transitionToWelcome());
    fetch.onPlayerFound();
    EXPECT_FALSE(fetch.transitionToWelcome()) << "no lookup is outstanding after dismount";
}

// Shows "Unable to / Locate Asset" for the failure-page time, then returns to PIN entry.
inline void expectFailurePageThenPinEntry(PlayerFetchTestSuite* suite, FetchUserState* fetch) {
    fetch->onStateLoop(&suite->device);
    EXPECT_FALSE(fetch->transitionToPinEntry());
    fetch->onPlayerFound();
    EXPECT_FALSE(fetch->transitionToWelcome()) << "a late answer is ignored";
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch->onStateLoop(&suite->device);
    EXPECT_TRUE(fetch->transitionToPinEntry());
    dismountState(fetch, &suite->device);
    EXPECT_FALSE(fetch->transitionToPinEntry());
}

inline void fetchUserFailedLookupShowsFailurePage(PlayerFetchTestSuite* suite) {
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Unable to"), _, _));
    FetchUserState fetch(&suite->playerProvider);
    mountFetchUser(suite, &fetch);
    fetch.onPlayerLookupFailed();
    expectFailurePageThenPinEntry(suite, &fetch);
}

inline void fetchUserTimeoutShowsFailurePage(PlayerFetchTestSuite* suite) {
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Unable to"), _, _));
    FetchUserState fetch(&suite->playerProvider);
    mountFetchUser(suite, &fetch);
    suite->fakeClock->advance(USER_DATA_FETCH_TIMEOUT_MS);
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(1);
    expectFailurePageThenPinEntry(suite, &fetch);
}
