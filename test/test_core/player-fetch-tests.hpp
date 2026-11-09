#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <set>

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
    PdnProvisioningClient provisioningClient{device.wirelessManager};
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    std::vector<HttpRequest> queuedRequests;
};

/** A PROFILE_DELIVERED packet for TEST_PLAYER_ID named @p name, answering PIN 0007. */
inline PlayerProvisionPacket makeProvisionPacket(const char* name) {
    Player player{};
    UUIDBytes playerId{};
    UUIDBytes::parse(TEST_PLAYER_ID, playerId);
    Team team{};
    strncpy(team.name, "Hunter", sizeof(team.name) - 1);
    player.applyRegistrationProfile(playerId, name, team, {});
    PlayerProvisionPacket pkt = PlayerProvisionCodec::encode(player);
    strncpy(pkt.pin, "0007", sizeof(pkt.pin) - 1);
    return pkt;
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
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
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
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
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
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
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

inline void fetchUserIgnoresUncabledOrMalformedProvision(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    PlayerProvisionPacket otherPin = makeProvisionPacket("Stale");
    strncpy(otherPin.pin, "4321", sizeof(otherPin.pin) - 1);
    suite->device.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&otherPin),
                                                       sizeof(otherPin));
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "a reply to an earlier PIN is not applied";

    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    PlayerProvisionPacket spoofed = makeProvisionPacket("Spoofed");
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
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
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
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
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
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    ASSERT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::WIFI);

    suite->fakeClock->advance(7000);
    fdnProv.sync();
    EXPECT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::WIFI);

    suite->fakeClock->advance(1500);
    fdnProv.sync();
    EXPECT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW)
    << "a lookup that never answers must not keep the FDN off ESP-NOW";

    // Leaving Wi-Fi fails the queued request, as the HTTP driver's disconnect does.
    suite->queuedRequests[0].onError({WirelessError::CONNECTION_FAILED, "Disconnected"});
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry())
    << "LOOKUP_FAILED reaches the PDN before its own 10 s timeout would";
    fetch.onStateDismounted(&suite->device);

    PlayerProvisionRequestPacket retry{};
    strncpy(retry.pin, "1234", sizeof(retry.pin) - 1);
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(
    suite->pdnMac, PktType::kPlayerProvisionRequest, reinterpret_cast<const uint8_t*>(&retry),
    sizeof(retry));
    EXPECT_EQ(suite->queuedRequests.size(), 2u) << "the timed-out lookup no longer blocks the next";
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

inline void chooseTeamConfirmEntersHub(PlayerFetchTestSuite* suite) {
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
    EXPECT_TRUE(chooseTeam.transitionToHub());
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

    // A demo Hunter keeps the default role but still tells peers it is registered.
    Player demo{};
    int demoNotifications = 0;
    demo.setOnRoleChanged([&demoNotifications]() { demoNotifications++; });
    Team hunter{};
    strncpy(hunter.name, "Hunter", sizeof(hunter.name) - 1);
    demo.applyRegistrationProfile(UUIDBytes(), "Demo", hunter, {});
    EXPECT_EQ(demoNotifications, 1);
    EXPECT_EQ(demo.toProfile().registered, 1);
}

inline void fdnSeesUnprovisionedPdnUntilItRegisters(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    PlayerProfile unregistered{};
    unregistered.userId = 0xFFFF;
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, unregistered);
    EXPECT_TRUE(fdnProv.hasUnprovisionedPdnCabled());
    EXPECT_FALSE(fdnProv.hasRegisteredPdnCabled());

    Player registered{};
    UUIDBytes playerId{};
    UUIDBytes::parse(TEST_PLAYER_ID, playerId);
    registered.applyRegistrationProfile(playerId, "Ada", Team{}, {});
    PlayerProfile profile = registered.toProfile();
    EXPECT_EQ(memcmp(profile.playerId, playerId.bytes, UUIDBytes::SIZE), 0)
    << "a registered PDN advertises its server id in the connection context";
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, profile);
    EXPECT_FALSE(fdnProv.hasUnprovisionedPdnCabled());

    Player demo{};
    Team hunter{};
    strncpy(hunter.name, "Hunter", sizeof(hunter.name) - 1);
    demo.applyRegistrationProfile(UUIDBytes(), "Demo", hunter, {});
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, demo.toProfile());
    EXPECT_FALSE(fdnProv.hasUnprovisionedPdnCabled())
    << "a demo player has no server id but is not waiting to register";
    EXPECT_TRUE(fdnProv.hasRegisteredPdnCabled()) << "and it keeps the symbol-match game open";

    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, unregistered);
    EXPECT_FALSE(fdnProv.hasUnprovisionedPdnCabled()) << "only a Connected jack counts";
}

inline void fetchUserResendsAbandonedRequestWithinOneReplyWindow(PlayerFetchTestSuite* suite) {
    std::vector<uint8_t> requestSeqIds;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kPlayerProvisionRequest, _, _))
    .WillByDefault(
    Invoke([&requestSeqIds](const uint8_t*, PktType, const uint8_t* data, const size_t) {
        requestSeqIds.push_back(data[0]);
        return 1;
    }));
    suite->cableFdn(false);
    suite->playerProvider.setRegistrationPin("1234");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    // Cabled late: the reply window starts with the request, not at mount.
    suite->fakeClock->advance(9000);
    suite->cableFdn(true);
    fetch.onStateLoop(&suite->device);
    ASSERT_EQ(requestSeqIds.size(), 1u);

    // The FDN is deaf on Wi-Fi, so no SEND_SUCCESS: the radio gives up after 1500 ms.
    for (int step = 0; step < 20; ++step) {
        suite->fakeClock->advance(100);
        fetch.onStateLoop(&suite->device);
    }
    std::set<uint8_t> distinct(requestSeqIds.begin(), requestSeqIds.end());
    EXPECT_GE(distinct.size(), 2u) << "an abandoned request is sent again as a fresh frame";
    EXPECT_FALSE(fetch.transitionToPinEntry()) << "11 s after mount, 2 s after the first send";

    // Resends do not extend the window: it closes 10 s after the first send.
    suite->fakeClock->advance(8100);
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fdnReportsLookupResultAndResendsUnconfirmedReply(PlayerFetchTestSuite* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    std::vector<std::string> results;
    fdnProv.setOnLookupDone(
    [&results](const char* name) { results.push_back(name != nullptr ? name : "<failed>"); });
    int replyFrames = 0;
    ON_CALL(*suite->fdnDevice.mockPeerComms, sendData(_, PktType::kPlayerProvision, _, _))
    .WillByDefault(Invoke([&replyFrames](const uint8_t*, PktType, const uint8_t*, const size_t) {
        replyFrames++;
        return 1;
    }));

    PlayerProvisionRequestPacket request{};
    strncpy(request.pin, "0007", sizeof(request.pin) - 1);
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(
    suite->pdnMac, PktType::kPlayerProvisionRequest, reinterpret_cast<const uint8_t*>(&request),
    sizeof(request));
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_TRUE(fdnProv.isLookupInFlight());

    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_EQ(results, (std::vector<std::string>{"Ada"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());
    EXPECT_EQ(replyFrames, 1);

    // No SEND_SUCCESS came back, so the reply goes out again on the backoff.
    suite->fakeClock->advance(150);
    fdnProv.sync();
    EXPECT_EQ(replyFrames, 2) << "a reply the radio did not confirm is resent";

    request.seqId = 9;
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(
    suite->pdnMac, PktType::kPlayerProvisionRequest, reinterpret_cast<const uint8_t*>(&request),
    sizeof(request));
    ASSERT_EQ(suite->queuedRequests.size(), 2u);
    suite->queuedRequests[1].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404"});
    EXPECT_EQ(results, (std::vector<std::string>{"Ada", "<failed>"}));
}
