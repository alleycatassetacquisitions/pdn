#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "device-player-codec-tests.hpp"
#include "utility-tests.hpp"
#include "game/player-provider.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class PlayerFetchTestSuite : public testing::Test {
public:
    /** Wires a PDN and a cabled FDN whose provision frames route to each other. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(0);

        ON_CALL(*fdnDevice.mockHttpClient, queueRequest(testing::_))
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

        ON_CALL(*device.mockPeerComms, getPeerCommsState())
        .WillByDefault(Return(PeerCommsState::ENABLED));
        ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
        .WillByDefault(Return(PeerCommsState::ENABLED));

        cableFdn(true);
        fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
        fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
        fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);

        ON_CALL(*device.mockPeerComms, sendData(_, PktType::kPlayerProvisionRequest, _, _))
        .WillByDefault(
        Invoke([this](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
            fdnDevice.mockPeerComms->triggerPacketCallback(pdnMac, PktType::kPlayerProvisionRequest,
                                                           data, len);
            return 1;
        }));
        ON_CALL(*fdnDevice.mockPeerComms, sendData(_, PktType::kPlayerProvision, _, _))
        .WillByDefault(
        Invoke([this](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
            device.mockPeerComms->triggerPacketCallback(fdnMac, PktType::kPlayerProvision, data,
                                                        len);
            return 1;
        }));
    }

    /** Tears down heap fixtures and clears queued HTTP requests. */
    void TearDown() override {
        queuedRequests.clear();
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** Plugs or unplugs the FDN on the PDN's input jack. */
    void cableFdn(bool connected) {
        device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::INPUT_JACK, connected ? JackStatus::CONNECTED : JackStatus::DISCONNECTED);
        device.fakeRemoteDeviceCoordinator.setPeerDeviceType(SerialIdentifier::INPUT_JACK,
                                                             DeviceType::FDN);
        device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);
    }

    FakePlatformClock* fakeClock = nullptr;
    PlayerProvider playerProvider;
    MockDevice device;
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    std::vector<HttpRequest> queuedRequests;
};

/** A PROFILE_DELIVERED packet for TEST_PLAYER_ID named @p name. */
inline PlayerProvisionPacket makeProvisionPacket(const char* name, int64_t serverUnixMs) {
    Player player{};
    UUIDBytes playerId{};
    UUIDBytes::parse(TEST_PLAYER_ID, playerId);
    Team team{};
    strncpy(team.name, "Hunter", sizeof(team.name) - 1);
    player.applyRegistrationProfile(playerId, name, team, {});
    return PlayerProvisionCodec::encode(player, serverUnixMs);
}

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

    PinEntryState pinEntry(&suite->playerProvider);
    const int digits[4] = {0, 0, 0, 7};
    completePinOnEntry(&suite->device, &pinEntry, primaryPress, secondaryPress, ctx, digits);
    EXPECT_STREQ(suite->playerProvider.getRegistrationPin(), "0007");
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

    PinEntryState pinEntry(&suite->playerProvider);
    const int digits[4] = {0, 0, 0, 0};
    completePinOnEntry(&suite->device, &pinEntry, primaryPress, secondaryPress, ctx, digits);
    EXPECT_STREQ(suite->playerProvider.getRegistrationPin(), DEMO_PIN);
    EXPECT_TRUE(pinEntry.transitionToChooseTeam());
    EXPECT_FALSE(pinEntry.transitionToFetchUser());
    pinEntry.onStateDismounted(&suite->device);
}

inline void fetchUserRequestsLookupFromCabledFdn(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->playerProvider.setRegistrationPin("1234");
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-pin/1234");
    EXPECT_TRUE(suite->queuedRequests[0].payload.empty());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserRequestsOnceFdnIsCabled(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->cableFdn(false);
    suite->playerProvider.setRegistrationPin("1234");
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    EXPECT_TRUE(suite->queuedRequests.empty());

    suite->cableFdn(true);
    fetch.onStateLoop(&suite->device);
    fetch.onStateLoop(&suite->device);
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "one request per mount";
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserStoresProvisionedPlayerOnSuccess(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    Player* player = suite->playerProvider.getPlayer();
    bool roleCallbackFired = false;
    player->setOnRoleChanged([&roleCallbackFired]() { roleCallbackFired = true; });

    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);

    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Bounty");
    appendTestAffiliationNamed(body, "Reboot Syndicate");
    suite->queuedRequests[0].onSuccess(body);

    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_STREQ(player->getServerPlayerName(), "Ada");
    EXPECT_FALSE(player->isHunter());
    EXPECT_EQ(player->getAllegiance(), Allegiance::RESISTANCE);
    EXPECT_TRUE(roleCallbackFired) << "applying registration profile must notify connected peers";
    EXPECT_STREQ(player->getUserID().c_str(), "7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11");
    EXPECT_STREQ(suite->playerProvider.getRegistrationPin(), "0007");
    EXPECT_TRUE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserSetsWallClockFromProvision(PlayerFetchTestSuite* suite) {
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    PlayerProvisionPacket preSync = makeProvisionPacket("Ada", 5000);
    suite->device.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&preSync),
                                                       sizeof(preSync));
    EXPECT_EQ(suite->fakeClock->unixMilliseconds(), 0) << "an unsynced FDN clock is ignored";

    PlayerProvisionPacket synced = makeProvisionPacket("Ada", 1792359000000LL);
    suite->device.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&synced),
                                                       sizeof(synced));
    EXPECT_EQ(suite->fakeClock->unixMilliseconds(), 1792359000000LL);
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserIgnoresUncabledOrMalformedProvision(PlayerFetchTestSuite* suite) {
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    PlayerProvisionPacket spoofed = makeProvisionPacket("Spoofed", 0);
    suite->device.mockPeerComms->triggerPacketCallback(strangerMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&spoofed),
                                                       sizeof(spoofed));
    EXPECT_FALSE(suite->playerProvider.hasPlayer());

    PlayerProvisionPacket unnamed = spoofed;
    unnamed.playerName[0] = '\0';
    suite->device.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&unnamed),
                                                       sizeof(unnamed));
    suite->device.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&spoofed),
                                                       sizeof(spoofed) - 1);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserFailureReturnsToPinAfterPageTimeout(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->playerProvider.setRegistrationPin("9999");
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404"});

    fetch.onStateLoop(&suite->device);
    EXPECT_FALSE(fetch.transitionToPinEntry());

    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserTimesOutWithoutFdn(PlayerFetchTestSuite* suite) {
    suite->cableFdn(false);
    suite->playerProvider.setRegistrationPin("1234");
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    suite->fakeClock->advance(USER_DATA_FETCH_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fdnLookupDeadlineLeavesWifi(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->playerProvider.setRegistrationPin("1234");
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    ASSERT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::WIFI);

    suite->fakeClock->advance(14000);
    fdnProv.sync();
    EXPECT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::WIFI);

    suite->fakeClock->advance(2000);
    fdnProv.sync();
    EXPECT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW)
    << "a lookup that never answers must not keep the FDN off ESP-NOW";

    // Leaving Wi-Fi fails the queued request, as the HTTP driver's disconnect does.
    suite->queuedRequests[0].onError({WirelessError::CONNECTION_FAILED, "Disconnected"});
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fdnDropsMalformedOrUncabledProvisionRequest(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    PlayerProvisionRequestPacket malformed{};
    strncpy(malformed.pin, "12/4", sizeof(malformed.pin) - 1);
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(
    suite->pdnMac, PktType::kPlayerProvisionRequest, reinterpret_cast<const uint8_t*>(&malformed),
    sizeof(malformed));
    EXPECT_TRUE(suite->queuedRequests.empty());

    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    PlayerProvisionRequestPacket valid{};
    strncpy(valid.pin, "1234", sizeof(valid.pin) - 1);
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(
    strangerMac, PktType::kPlayerProvisionRequest, reinterpret_cast<const uint8_t*>(&valid),
    sizeof(valid));
    EXPECT_TRUE(suite->queuedRequests.empty());
}

inline void chooseTeamConfirmReachesDemoStub(PlayerFetchTestSuite* suite) {
    ChooseTeamState chooseTeam(&suite->playerProvider);
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

inline void reprovisionNotifiesRoleChangedEvenWhenRoleUnchanged(PlayerFetchTestSuite* suite) {
    Player* player = suite->playerProvider.getPlayer();
    int roleNotifications = 0;
    player->setOnRoleChanged([&roleNotifications]() { roleNotifications++; });

    UUIDBytes firstId{};
    UUIDBytes::parse(TEST_PLAYER_ID, firstId);
    Team team{};
    strncpy(team.name, "Bounty", sizeof(team.name) - 1);
    player->applyRegistrationProfile(firstId, "FirstAsset", team, {});
    EXPECT_EQ(roleNotifications, 1);

    UUIDBytes secondId{};
    UUIDBytes::parse("11111111-2222-4000-8000-333333333333", secondId);
    player->applyRegistrationProfile(secondId, "SecondAsset", team, {});
    EXPECT_EQ(roleNotifications, 2);
    EXPECT_STREQ(player->getServerPlayerName(), "SecondAsset");
}
