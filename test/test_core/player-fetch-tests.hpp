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
    /** Wires mocks and a fake platform clock for cabled FDN-to-PDN PlayerFetch tests. */
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

        device.fakeRemoteDeviceCoordinator.setJackStatus(SerialIdentifier::INPUT_JACK,
                                                         JackStatus::CONNECTED);
        device.fakeRemoteDeviceCoordinator.setPeerDeviceType(SerialIdentifier::INPUT_JACK,
                                                             DeviceType::FDN);
        device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);

        fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
        fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
        fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);

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

    FakePlatformClock* fakeClock = nullptr;
    PlayerProvider playerProvider;
    MockDevice device;
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    std::vector<HttpRequest> queuedRequests;
};

inline void pinEntryRoutesNonDemoPinToFetch(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    EXPECT_FALSE(fdnProv.provisionConnectedPdnByPin("123")) << "3-digit PIN must be rejected";
    EXPECT_FALSE(fdnProv.provisionConnectedPdnByPin("12a4")) << "non-digit PIN must be rejected";
    EXPECT_TRUE(suite->queuedRequests.empty());

    EXPECT_TRUE(fdnProv.provisionConnectedPdnByPin("0007"));
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-pin/0007");
}

inline void pinEntryRoutesDemoPinToChooseTeam(PlayerFetchTestSuite* suite) {
    // Unprovisioned PDN starts with userId == 0xFFFF and zero UUIDs; provisioning assigns a
    // non-0xFFFF userId
    Player* player = suite->playerProvider.getPlayer();
    PlayerProfile unprov = player->toProfile();
    EXPECT_EQ(unprov.userId, 0xFFFFu);

    bool roleCallbackFired = false;
    player->setOnRoleChanged([&roleCallbackFired]() { roleCallbackFired = true; });

    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    ASSERT_TRUE(fdnProv.provisionConnectedPdnByPin("0000"));
    ASSERT_EQ(suite->queuedRequests.size(), 1u);

    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "RebootAsset");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Bounty");
    appendTestAffiliationNamed(body, "Reboot Syndicate");
    suite->queuedRequests[0].onSuccess(body);

    EXPECT_TRUE(roleCallbackFired) << "applying registration profile must notify connected peers";
    EXPECT_FALSE(player->isHunter());
    EXPECT_EQ(player->getAllegiance(), Allegiance::RESISTANCE);
    EXPECT_NE(player->toProfile().userId, 0xFFFFu);
    EXPECT_TRUE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserQueuesDevicePlayerLookup(PlayerFetchTestSuite* suite) {
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    // Uncabled sender is ignored
    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    Player validPlayer{};
    UUIDBytes playerId{};
    UUIDBytes::parse(TEST_PLAYER_ID, playerId);
    Team team{};
    strncpy(team.name, "Hunter", sizeof(team.name) - 1);
    validPlayer.applyRegistrationProfile(playerId, "Spoofed", team, {});
    PlayerProvisionPacket spoofPkt = FdnProvisioningManager::toProvisionPacket(validPlayer);
    suite->device.mockPeerComms->triggerPacketCallback(strangerMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&spoofPkt),
                                                       sizeof(spoofPkt));
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(fetch.transitionToWelcome());

    // Malformed packet (empty playerName) from cabled FDN is also ignored
    PlayerProvisionPacket badPkt = spoofPkt;
    badPkt.playerName[0] = '\0';
    suite->device.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&badPkt),
                                                       sizeof(badPkt));
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(fetch.transitionToWelcome());

    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserStoresDevicePlayerOnSuccess(PlayerFetchTestSuite* suite) {
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    ASSERT_TRUE(fdnProv.provisionConnectedPdnByPin("0007"));
    ASSERT_EQ(suite->queuedRequests.size(), 1u);

    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Hunter");
    suite->queuedRequests[0].onSuccess(body);

    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Ada");
    EXPECT_TRUE(suite->playerProvider.getPlayer()->isHunter());
    EXPECT_TRUE(fetch.transitionToWelcome());
    suite->queuedRequests.clear();
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserFailureReturnsToPinAfterPageTimeout(PlayerFetchTestSuite* suite) {
    FetchUserState fetch(suite->device.wirelessManager, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    ASSERT_TRUE(fdnProv.provisionConnectedPdnByPin("9999"));
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404"});

    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.isShowingFailurePage());
    EXPECT_FALSE(fetch.transitionToWelcome());

    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_FALSE(fetch.isShowingFailurePage());
    suite->queuedRequests.clear();
    fetch.onStateDismounted(&suite->device);
}

inline void chooseTeamConfirmReachesDemoStub(PlayerFetchTestSuite* suite) {
    // Re-provisioning overwrites identity and notifies connected peers even when role is unchanged
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
