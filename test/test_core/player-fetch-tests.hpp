#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <set>
#include <algorithm>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "device-player-codec-tests.hpp"
#include "fdn-provisioning-tests.hpp"
#include "utility-tests.hpp"
#include "game/player-provider.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class PlayerFetchTestSuite : public FdnProvisioningTests {
public:
    /** Adds a PDN to the cabled FDN; provision frames route between the two. */
    void SetUp() override {
        FdnProvisioningTests::SetUp();

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, renderGlyph(_, _, _))
        .WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawButton(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());

        ON_CALL(*device.mockPeerComms, getPeerCommsState())
        .WillByDefault(Return(PeerCommsState::ENABLED));

        cableFdn(true);

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

    /** Plugs or unplugs the FDN on the PDN's input jack. */
    void cableFdn(bool connected) {
        device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::INPUT_JACK, connected ? JackStatus::CONNECTED : JackStatus::DISCONNECTED);
        device.fakeRemoteDeviceCoordinator.setPeerDeviceType(SerialIdentifier::INPUT_JACK,
                                                             DeviceType::FDN);
        device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);
    }

    /** Hands the PDN one record key frame as arrived on @p jack. */
    void deliverRecordKey(const ApplicationPayload& key,
                          SerialIdentifier jack = SerialIdentifier::INPUT_JACK) {
        device.fakeRemoteDeviceCoordinator.deliverApplicationFrame(jack, key);
    }

    /**
     * Carries the key frame the FDN last wrote to its jack over to the PDN's
     * jack, then has the FDN's jack report the frame written.
     */
    void carryRecordKeyAcrossJack() {
        ASSERT_FALSE(fdnRdc.getSentApplicationFrames().empty()) << "the FDN wrote no key frame";
        deliverRecordKey(fdnRdc.getSentApplicationFrames().back().second);
        fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    }

    /** Hands the PDN @p pkt over the radio as sent by @p fromMac. */
    void deliverProvision(const PlayerProvisionPacket& pkt, const uint8_t* fromMac) {
        device.mockPeerComms->triggerPacketCallback(
        fromMac, PktType::kPlayerProvision, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    }

    /** @return true when the session holds exactly @p key. */
    bool holdsRecordKey(const ApplicationPayload& key) {
        RecordKey* recordKey = playerProvider.getRecordKey();
        return recordKey->isHeld() && memcmp(recordKey->getBytes(), key.data(), key.size()) == 0;
    }

    PlayerProvider playerProvider;
    MockDevice device;
    PdnProvisioningClient provisioningClient{device.wirelessManager};
};

// A record key counting up from `firstByte`, so two keys are told apart.
inline ApplicationPayload makeTestRecordKey(uint8_t firstByte) {
    ApplicationPayload key{};
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(firstByte + i);
    }
    return key;
}

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
    EXPECT_EQ(suite->queuedRequests.size(), 1u)
    << "a second loop does not re-send while the first request is in flight";
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
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "the profile arrived ahead of the key";
    suite->carryRecordKeyAcrossJack();

    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_STREQ(player->getServerPlayerName(), "Ada");
    EXPECT_FALSE(player->isHunter());
    EXPECT_EQ(player->getAllegiance(), Allegiance::RESISTANCE);
    EXPECT_TRUE(roleCallbackFired) << "applying registration profile must notify connected peers";
    EXPECT_STREQ(player->getUserID().c_str(), "7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11");
    EXPECT_STREQ(suite->playerProvider.getRegistrationPin(), "0007");
    EXPECT_TRUE(holdsTestRecordKey(*suite->playerProvider.getRecordKey()))
    << "the PDN does not hold the key the server issued";
    EXPECT_TRUE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserIgnoresUncabledOrMalformedProvision(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    // With a key in hand, only the packet checks stand between these and the player.
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    ASSERT_TRUE(suite->playerProvider.getRecordKey()->isHeld());

    PlayerProvisionPacket otherPin = makeProvisionPacket("Stale");
    strncpy(otherPin.pin, "4321", sizeof(otherPin.pin) - 1);
    suite->deliverProvision(otherPin, suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "a reply to an earlier PIN is not applied";

    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    PlayerProvisionPacket spoofed = makeProvisionPacket("Spoofed");
    suite->deliverProvision(spoofed, strangerMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());

    PlayerProvisionPacket unnamed = spoofed;
    unnamed.playerName[0] = '\0';
    suite->deliverProvision(unnamed, suite->fdnMac);
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

    PlayerProvisionPacket late = makeProvisionPacket("Late");
    strncpy(late.pin, "9999", sizeof(late.pin) - 1);
    suite->deliverProvision(late, suite->fdnMac);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(fetch.transitionToWelcome())
    << "a late packet or key after the failure page started must not flip to welcome";
    EXPECT_FALSE(suite->playerProvider.hasPlayer());

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
        suite->device.wirelessManager->sync();
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

inline void fetchUserRegistersOnKeyThenProfile(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    const ApplicationPayload key = makeTestRecordKey(0xA0);
    suite->deliverRecordKey(key);
    EXPECT_FALSE(fetch.transitionToWelcome()) << "a key alone registers nobody";
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);

    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_TRUE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);
    EXPECT_TRUE(suite->holdsRecordKey(key)) << "the key serves the session after registration";
}

// The key crosses the jack and the profile the radio, so the profile can win the race.
inline void fetchUserRegistersOnProfileThenKey(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    fetch.onStateLoop(&suite->device);
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "a profile is not applied without a key";
    EXPECT_FALSE(fetch.transitionToWelcome());
    EXPECT_FALSE(fetch.transitionToPinEntry());

    const ApplicationPayload key = makeTestRecordKey(0xA0);
    suite->deliverRecordKey(key);
    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Ada");
    EXPECT_TRUE(fetch.transitionToWelcome());
    EXPECT_TRUE(suite->holdsRecordKey(key));
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserProfileWithoutKeyFailsAtTimeLimit(PlayerFetchTestSuite* suite) {
    EXPECT_CALL(*suite->device.mockDisplay, drawText(_, _, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Unable to"), _, _)).Times(1);
    EXPECT_CALL(*suite->device.mockDisplay, drawText(testing::StrEq("Locate Asset"), _, _))
    .Times(1);
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    suite->fakeClock->advance(USER_DATA_FETCH_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer())
    << "a key after the attempt failed must not revive its profile";
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    EXPECT_FALSE(fetch.transitionToWelcome());

    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserKeyWithoutProfileIsClearedAtTimeLimit(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    ASSERT_TRUE(suite->playerProvider.getRecordKey()->isHeld());

    suite->fakeClock->advance(USER_DATA_FETCH_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());

    suite->deliverProvision(makeProvisionPacket("Late"), suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserLookupFailureDropsKeyAndHeldProfile(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    PlayerProvisionPacket failed{};
    failed.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
    strncpy(failed.pin, "0007", sizeof(failed.pin) - 1);

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverProvision(failed, suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "the key outlived the registration that failed";
    fetch.onStateDismounted(&suite->device);

    fetch.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    suite->deliverProvision(failed, suite->fdnMac);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    EXPECT_FALSE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserAcceptsKeyOnlyFromTheFdnItAsked(PlayerFetchTestSuite* suite) {
    const uint8_t otherMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x02};
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    suite->cableFdn(false);
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    suite->deliverRecordKey(makeTestRecordKey(0x10));
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "no FDN is Connected and no PIN has been sent";

    suite->cableFdn(true);
    fetch.onStateLoop(&suite->device);

    // A jack can name its peer without being Connected (the real one does from CONNECTING).
    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, suite->fdnMac);
    suite->deliverRecordKey(makeTestRecordKey(0x20), SerialIdentifier::OUTPUT_JACK);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "the FDN is not Connected on that jack";

    // A HELLO's source MAC is self-asserted, so a PDN peer can claim the FDN's.
    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::PDN);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    suite->deliverRecordKey(makeTestRecordKey(0x30), SerialIdentifier::OUTPUT_JACK);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld()) << "a PDN peer wrote it";

    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, otherMac);
    suite->deliverRecordKey(makeTestRecordKey(0x40), SerialIdentifier::OUTPUT_JACK);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "an FDN that was never sent the PIN wrote it";

    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());

    const ApplicationPayload key = makeTestRecordKey(0x50);
    suite->deliverRecordKey(key);
    EXPECT_TRUE(suite->holdsRecordKey(key));
    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserLaterKeyReplacesHeldKey(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    const ApplicationPayload later = makeTestRecordKey(0x60);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverRecordKey(later);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_TRUE(suite->holdsRecordKey(later));

    // Registration is complete: a frame after it belongs to no attempt.
    suite->deliverRecordKey(makeTestRecordKey(0x10));
    EXPECT_TRUE(suite->holdsRecordKey(later));
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserJackLossAfterKeyFailsAttempt(PlayerFetchTestSuite* suite) {
    const uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::PDN);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, peerMac);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    ASSERT_TRUE(suite->playerProvider.getRecordKey()->isHeld())
    << "a peer leaving the other jack is no concern of the registration";

    suite->cableFdn(false);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    suite->cableFdn(true);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer())
    << "the attempt ended when the FDN's jack dropped";
    EXPECT_FALSE(fetch.transitionToWelcome());

    // Well inside the 10 s reply window: the failure does not wait for it.
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserJackLossWithProfileHeldFailsAttempt(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);

    suite->cableFdn(false);
    suite->cableFdn(true);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer())
    << "the attempt ended when the FDN's jack dropped";
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());

    // Well inside the 10 s reply window: the failure does not wait for it.
    fetch.onStateLoop(&suite->device);
    suite->fakeClock->advance(FETCH_FAILURE_PAGE_TIMEOUT_MS + 1);
    fetch.onStateLoop(&suite->device);
    EXPECT_TRUE(fetch.transitionToPinEntry());
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserSendsNoPinAfterRegistering(PlayerFetchTestSuite* suite) {
    std::set<uint8_t> requestSeqIds;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kPlayerProvisionRequest, _, _))
    .WillByDefault(
    Invoke([&requestSeqIds](const uint8_t*, PktType, const uint8_t* data, const size_t) {
        requestSeqIds.insert(data[0]);
        return 1;
    }));
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    ASSERT_TRUE(fetch.transitionToWelcome());

    // No SEND_SUCCESS ever came for the request, so the radio gives up on it after 1500 ms.
    for (int step = 0; step < 20; ++step) {
        suite->fakeClock->advance(100);
        suite->device.wirelessManager->sync();
    }
    ASSERT_TRUE(suite->provisioningClient.isRequestAbandoned());
    fetch.onStateLoop(&suite->device);
    EXPECT_EQ(requestSeqIds.size(), 1u) << "a registered PDN asked to be registered again";
    fetch.onStateDismounted(&suite->device);
}

inline void fetchUserLeavingAttemptDropsKeyAndProfile(PlayerFetchTestSuite* suite) {
    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    ASSERT_TRUE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);

    // Left mid-attempt from here on, as when another app is switched in before the FDN answers.
    fetch.onStateMounted(&suite->device);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "a new attempt started with the last session's key";
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(fetch.transitionToWelcome()) << "the last attempt's profile completed this one";
    fetch.onStateDismounted(&suite->device);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());

    fetch.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    fetch.onStateDismounted(&suite->device);
    fetch.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(fetch.transitionToWelcome()) << "a profile outlived the attempt it answered";
    fetch.onStateDismounted(&suite->device);
}

inline void pinEntryHoldsNoRecordKey(PlayerFetchTestSuite* suite) {
    suite->playerProvider.getRecordKey()->set(makeTestRecordKey(0xA0).data());
    PinEntryState pinEntry(&suite->playerProvider);
    pinEntry.onStateMounted(&suite->device);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "re-entering PIN entry kept the last registration's key";

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "a key was accepted before any PIN was sent";
    pinEntry.onStateDismounted(&suite->device);
}

inline void registrationWritesNoRecordKeyToStorage(PlayerFetchTestSuite* suite) {
    std::vector<std::string> written;
    ON_CALL(*suite->device.mockStorage, writeBytes(_, _, _, _))
    .WillByDefault(
    Invoke([&written](const std::string&, const std::string&, const uint8_t* data, size_t length) {
        written.emplace_back(reinterpret_cast<const char*>(data), length);
        return length;
    }));
    ON_CALL(*suite->device.mockStorage, write(_, _, _))
    .WillByDefault(
    Invoke([&written](const std::string&, const std::string&, const std::string& value) {
        written.push_back(value);
        return value.size();
    }));

    suite->playerProvider.setRegistrationPin("0007");
    FetchUserState fetch(&suite->provisioningClient, &suite->playerProvider);
    fetch.onStateMounted(&suite->device);
    const ApplicationPayload key = makeTestRecordKey(0xA0);
    suite->deliverRecordKey(key);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    fetch.onStateLoop(&suite->device);
    ASSERT_TRUE(fetch.transitionToWelcome());
    fetch.onStateDismounted(&suite->device);

    PlayerFetchWelcome welcome(&suite->playerProvider);
    welcome.onStateMounted(&suite->device);
    suite->fakeClock->advance(WELCOME_MESSAGE_TIMEOUT_MS + 1);
    welcome.onStateLoop(&suite->device);
    welcome.onStateDismounted(&suite->device);

    // Half the key is already too much of it to find in storage.
    const std::string keyHalf(reinterpret_cast<const char*>(key.data()), key.size() / 2);
    for (const std::string& value : written) {
        EXPECT_EQ(value.find(keyHalf), std::string::npos) << "the record key reached storage";
    }
}
