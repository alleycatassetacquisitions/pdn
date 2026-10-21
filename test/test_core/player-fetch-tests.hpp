#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "alleycat-protobuf-test-helpers.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "device-player-decoder-tests.hpp"
#include "utility-tests.hpp"

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

        player = new Player();
        ON_CALL(*device.mockHttpClient, queueRequest(testing::_))
        .WillByDefault(Invoke([this](HttpRequest&& request) {
            queuedRequests.push_back(std::move(request));
            return true;
        }));

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, renderGlyph(_, _, _))
        .WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawButton(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());
    }

    /** Tears down heap fixtures and clears queued HTTP requests. */
    void TearDown() override {
        queuedRequests.clear();
        delete player;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakePlatformClock* fakeClock = nullptr;
    Player* player = nullptr;
    MockDevice device;
    std::vector<HttpRequest> queuedRequests;
};

inline void completePinOnEntry(PDN* pdn, PinEntryState* pinEntry,
                               parameterizedCallbackFunction& primaryPress,
                               parameterizedCallbackFunction& secondaryPress, void*& ctx,
                               const int digits[4]) {
    pinEntry->onStateMounted(pdn);
    ASSERT_NE(primaryPress, nullptr);
    ASSERT_NE(secondaryPress, nullptr);
    for (int index = 0; index < 4; index++) {
        for (int press = 0; press < digits[index]; press++) {
            primaryPress(ctx);
        }
        secondaryPress(ctx);
    }
}

inline void pinEntryRoutesNonDemoPinToFetch(PlayerFetchTestSuite* suite) {
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

    PinEntryState pinEntry(suite->player);
    const int digits[4] = {0, 0, 0, 7};
    completePinOnEntry(&suite->device, &pinEntry, primaryPress, secondaryPress, ctx, digits);
    EXPECT_EQ(suite->player->getUserID(), "0007");
    EXPECT_TRUE(pinEntry.transitionToFetchUser());
    EXPECT_FALSE(pinEntry.transitionToChooseTeam());
    pinEntry.onStateDismounted(&suite->device);
}

inline void pinEntryRoutesDemoPinToChooseTeam(PlayerFetchTestSuite* suite) {
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

    PinEntryState pinEntry(suite->player);
    const int digits[4] = {0, 0, 0, 0};
    completePinOnEntry(&suite->device, &pinEntry, primaryPress, secondaryPress, ctx, digits);
    EXPECT_EQ(suite->player->getUserID(), DEMO_PIN);
    EXPECT_TRUE(pinEntry.transitionToChooseTeam());
    EXPECT_FALSE(pinEntry.transitionToFetchUser());
    pinEntry.onStateDismounted(&suite->device);
}

inline void fetchUserQueuesDevicePlayerLookup(PlayerFetchTestSuite* suite) {
    suite->player->setUserID(const_cast<char*>("1234"));
    PlayerProvider playerProvider;
    FetchUserState fetch(suite->player, suite->device.wirelessManager, &playerProvider);
    fetch.onStateMounted(&suite->device);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-pin/1234");
    EXPECT_TRUE(suite->queuedRequests[0].payload.empty());
    suite->queuedRequests.clear();
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserStoresDevicePlayerOnSuccess(PlayerFetchTestSuite* suite) {
    suite->player->setUserID(const_cast<char*>("0007"));
    PlayerProvider playerProvider;
    FetchUserState fetch(suite->player, suite->device.wirelessManager, &playerProvider);
    fetch.onStateMounted(&suite->device);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);

    suite->queuedRequests[0].onSuccess(
        encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));

    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(playerProvider.hasPlayer());
    EXPECT_STREQ(playerProvider.getPlayer().name, "Ada");
    EXPECT_TRUE(fetch.transitionToWelcome());
    suite->queuedRequests.clear();
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserFailureReturnsToPinAfterPageTimeout(PlayerFetchTestSuite* suite) {
    suite->player->setUserID(const_cast<char*>("9999"));
    PlayerProvider playerProvider;
    FetchUserState fetch(suite->player, suite->device.wirelessManager, &playerProvider);
    fetch.onStateMounted(&suite->device);
    suite->queuedRequests[0].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404"});

    fetch.onStateLoop(&suite->device);
    EXPECT_FALSE(fetch.transitionToPinEntry());

    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    suite->queuedRequests.clear();
    fetch.onStateDismounted(&suite->device);
}

inline void chooseTeamConfirmReachesDemoStub(PlayerFetchTestSuite* suite) {
    PlayerProvider playerProvider;
    ChooseTeamState chooseTeam(suite->player, &playerProvider);
    parameterizedCallbackFunction secondaryPress = nullptr;
    void* secondaryCtx = nullptr;
    ON_CALL(*suite->device.mockSecondaryButton, setButtonPress(_, _, _))
    .WillByDefault(Invoke([&](parameterizedCallbackFunction fn, void* ctx, ButtonInteraction) {
        secondaryPress = fn;
        secondaryCtx = ctx;
    }));
    chooseTeam.onStateMounted(&suite->device);
    ASSERT_NE(secondaryPress, nullptr);
    secondaryPress(secondaryCtx);
    EXPECT_TRUE(chooseTeam.transitionToDemoStub());
    chooseTeam.onStateDismounted(&suite->device);
}
