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
    PinEntryState pinEntry(&suite->playerProvider);
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
    EXPECT_FALSE(chooseTeam->transitionToHub());
    secondaryPress(secondaryCtx);
}

inline void chooseTeamTestIdentityRegistersMacPlayerAndEntersHub(PlayerFetchTestSuite* suite) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x07};
    ChooseTeamState chooseTeam(&suite->playerProvider, true);
    confirmChooseTeam(suite, &chooseTeam, mac, true);

    Player* player = suite->playerProvider.getPlayer();
    EXPECT_TRUE(chooseTeam.transitionToHub());
    EXPECT_TRUE(player->hasRegistrationProfile());
    EXPECT_EQ(player->getServerPlayerId(), Player::testPlayerIdFromMac(mac));
    EXPECT_FALSE(player->isHunter());
    EXPECT_STREQ(player->getServerPlayerName(), "Test");
    EXPECT_FALSE(player->hasTeam());

    dismountState(&chooseTeam, &suite->device);
    EXPECT_FALSE(chooseTeam.transitionToHub());
}

inline void chooseTeamDemoKeepsNilPlayerId(PlayerFetchTestSuite* suite) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x07};
    ChooseTeamState chooseTeam(&suite->playerProvider, false);
    confirmChooseTeam(suite, &chooseTeam, mac, false);

    Player* player = suite->playerProvider.getPlayer();
    EXPECT_TRUE(chooseTeam.transitionToHub());
    EXPECT_TRUE(player->getServerPlayerId().isZero());
    EXPECT_TRUE(player->isHunter());
    EXPECT_STREQ(player->getServerPlayerName(), "Demo");

    dismountState(&chooseTeam, &suite->device);
    EXPECT_FALSE(chooseTeam.transitionToHub());
}

inline void pinEntryAndFirmwareUpdateLeadToEachOther(PlayerFetchTestSuite* suite) {
    FakeFirmwareStore store;
    FirmwareUpdateManager manager(suite->device.mockPeerComms, &store);
    PlayerFetchApp app(&manager, &suite->playerProvider, false);
    app.populateStateMap();
    ASSERT_EQ(app.getStateMap().size(), 3u);
    State* pinEntry = app.getStateMap()[0];
    EXPECT_EQ(pinEntry->getTransitions()[1]->getNextState(), app.getStateMap()[2]);
    EXPECT_EQ(app.getStateMap()[2]->getTransitions()[0]->getNextState(), pinEntry);
}
