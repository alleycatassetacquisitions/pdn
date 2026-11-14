#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdlib>
#include <string>
#include <vector>

#include "alleycat-protobuf-test-helpers.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "device-player-codec-tests.hpp"
#include "fake-band-reader.hpp"
#include "fdn-provisioning-tests.hpp"
#include "registration-test-helper.hpp"
#include "utility-tests.hpp"
#include "game/player-provider.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class PlayerFetchTestSuite : public FdnProvisioningTests {
public:
    /** Adds a PDN cabled to the FDN; provision packets the FDN sends reach the PDN. */
    void SetUp() override {
        FdnProvisioningTests::SetUp();

        ON_CALL(*device.mockDisplay, invalidateScreen()).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawText(_, _, _))
        .WillByDefault(Invoke([this](const char* text, int, int) {
            drawnTexts.emplace_back(text);
            return device.mockDisplay;
        }));
        ON_CALL(*device.mockDisplay, setGlyphMode(_)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, renderGlyph(_, _, _))
        .WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, drawButton(_, _, _)).WillByDefault(Return(device.mockDisplay));
        ON_CALL(*device.mockDisplay, render()).WillByDefault(Return());

        ON_CALL(*device.mockPeerComms, getPeerCommsState())
        .WillByDefault(Return(PeerCommsState::ENABLED));

        cableFdn(true);

        ON_CALL(*fdnDevice.mockPeerComms, sendData(_, PktType::kPlayerProvision, _, _))
        .WillByDefault(
        Invoke([this](const uint8_t*, PktType, const uint8_t* data, const size_t len) {
            device.mockPeerComms->triggerPacketCallback(fdnMac, PktType::kPlayerProvision, data,
                                                        len);
            return 1;
        }));
    }

    /**
     * Plugs or unplugs the FDN on the PDN's input jack; its context carries
     * @p turn, open unless a test says otherwise.
     */
    void cableFdn(bool connected, RegistrationTurn turn = RegistrationTurn::OPEN) {
        setFdnTurn(turn);
        device.fakeRemoteDeviceCoordinator.setPeerMac(SerialIdentifier::INPUT_JACK, fdnMac);
        device.fakeRemoteDeviceCoordinator.setJackStatus(
        SerialIdentifier::INPUT_JACK, connected ? JackStatus::CONNECTED : JackStatus::DISCONNECTED);
    }

    /** Has the FDN on the PDN's input jack resend its context carrying @p turn. */
    void setFdnTurn(RegistrationTurn turn) {
        FdnProfile fdn{};
        fdn.registrationTurn = turn;
        device.fakeRemoteDeviceCoordinator.setPeerFdnProfile(SerialIdentifier::INPUT_JACK, fdn);
    }

    /** Puts a second FDN, @p mac, Connected on the PDN's output jack. */
    void cableOtherFdnOnOutputJack(const uint8_t* mac) {
        FakeRemoteDeviceCoordinator& rdc = device.fakeRemoteDeviceCoordinator;
        rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::FDN);
        rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, mac);
        rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
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

    /** @return true when @p top then @p bottom were the last two lines drawn. */
    bool lastScreenReads(const char* top, const char* bottom) const {
        return drawnTexts.size() >= 2 && drawnTexts[drawnTexts.size() - 2] == top &&
               drawnTexts.back() == bottom;
    }

    /** Runs one loop tick of @p awaitBand; @return true when it then shows the failure page. */
    bool failurePageShowsAfterTick(AwaitBandState& awaitBand) {
        awaitBand.onStateLoop(&device);
        return lastScreenReads("Unable to", "Locate Asset");
    }

    // The FDN's radio MAC, as the PDN sees it on its input jack.
    uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x01};
    PlayerProvider playerProvider;
    MockDevice device;
    std::vector<std::string> drawnTexts;
};

/** A PROFILE_DELIVERED packet for player @p playerIdText named @p name. */
inline PlayerProvisionPacket makeProvisionPacket(const char* name,
                                                 const char* playerIdText = TEST_PLAYER_ID) {
    Player player{};
    UUIDBytes playerId{};
    UUIDBytes::parse(playerIdText, playerId);
    Team team{};
    strncpy(team.name, "Hunter", sizeof(team.name) - 1);
    player.applyRegistrationProfile(playerId, name, team, {});
    return PlayerProvisionCodec::encode(player);
}

/** A LOOKUP_FAILED packet, as the FDN sends when a tap found nobody. */
inline PlayerProvisionPacket makeLookupFailedPacket() {
    PlayerProvisionPacket failed{};
    failed.status = static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED);
    return failed;
}

/**
 * Registers the suite's PDN through a real FDN: the PDN is queued at the FDN, a
 * band is tapped and the server answers the lookup with @p body.
 */
inline void registerThroughFdn(PlayerFetchTestSuite* suite, const std::string& body) {
    FakeBandReader reader;
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc, &reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, PlayerProfile{});
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    fdnProv.sync();
    reader.placeBand({0x04, 0xA2, 0x1B, 0x9C});
    fdnProv.sync();
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onSuccess(body);
    suite->carryRecordKeyAcrossJack();
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
}

// Cabled, the PDN asks for a tap only while the FDN says its jack's turn is open,
// and asks the player to wait otherwise.
inline void awaitBandAsksForTapOnlyWhileItsTurnIsOpen(PlayerFetchTestSuite* suite) {
    suite->cableFdn(false);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Cable to", "FDN"));

    suite->cableFdn(true, RegistrationTurn::CLOSED);
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Please", "wait")) << "cabled without the turn";

    suite->setFdnTurn(RegistrationTurn::OPEN);
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Tap band", "at FDN"));

    suite->setFdnTurn(RegistrationTurn::CLOSED);
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Please", "wait")) << "Tap band outlived the turn";

    suite->cableFdn(false);
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Cable to", "FDN"));
    awaitBand.onStateDismounted(&suite->device);
}

// An older FDN's context carries no turn, an FDN's context may not have arrived,
// and a turn byte may be junk: none of them is an open turn.
inline void awaitBandWaitsWhenTheFdnSaysNoOpenTurn(PlayerFetchTestSuite* suite) {
    suite->cableFdn(true, RegistrationTurn::NOT_SENT);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Please", "wait")) << "a context with no turn said tap";

    suite->setFdnTurn(static_cast<RegistrationTurn>(7));
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Please", "wait")) << "a junk turn byte said tap";
    awaitBand.onStateDismounted(&suite->device);

    const uint8_t otherFdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x02};
    suite->cableFdn(false);
    suite->cableOtherFdnOnOutputJack(otherFdnMac);
    awaitBand.onStateMounted(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Please", "wait")) << "an FDN with no context said tap";
    awaitBand.onStateDismounted(&suite->device);
}

// Two PDNs at one FDN: the screen of the suite's PDN, on the FDN's primary jack,
// follows the turn the FDN sends it, open and then moved to the other jack.
inline void awaitBandFollowsTheTurnTheFdnSends(PlayerFetchTestSuite* suite) {
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->cableFdn(true, RegistrationTurn::CLOSED);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    ASSERT_TRUE(suite->lastScreenReads("Please", "wait"));

    fdnProv.sync();
    suite->setFdnTurn(suite->lastTurnSentTo(SerialIdentifier::INPUT_JACK));
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Tap band", "at FDN")) << "the pending PDN was not asked";

    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 1);
    fdnProv.sync();
    suite->setFdnTurn(suite->lastTurnSentTo(SerialIdentifier::INPUT_JACK));
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Please", "wait"))
    << "the PDN kept asking for a tap after the turn moved to the other jack";
    awaitBand.onStateDismounted(&suite->device);
}

// A peer that is not an FDN, or an FDN not yet Connected, is no FDN to tap at.
inline void awaitBandAsksForCableUntilAnFdnIsConnected(PlayerFetchTestSuite* suite) {
    suite->cableFdn(false);
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    const uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::PDN);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, peerMac);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTING);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Cable to", "FDN"));
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandSendsNothingWhileWaiting(PlayerFetchTestSuite* suite) {
    EXPECT_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).Times(0);
    suite->cableFdn(false);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    awaitBand.onStateLoop(&suite->device);
    suite->cableFdn(true);
    for (int step = 0; step < 30; ++step) {
        suite->fakeClock->advance(1000);
        suite->device.wirelessManager->sync();
        awaitBand.onStateLoop(&suite->device);
    }
    awaitBand.onStateDismounted(&suite->device);
}

// With nothing accepted there is no attempt to time out: the tap may be a while.
inline void awaitBandWaitsForTheTapWithoutLimit(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    suite->fakeClock->advance(10 * REGISTRATION_ATTEMPT_TIMEOUT_MS);
    EXPECT_FALSE(suite->failurePageShowsAfterTick(awaitBand));

    // The attempt window runs from the first piece, not from the mount.
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->fakeClock->advance(REGISTRATION_ATTEMPT_TIMEOUT_MS - 1000);
    awaitBand.onStateLoop(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_TRUE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandStoresPlayerRegisteredThroughFdn(PlayerFetchTestSuite* suite) {
    Player* player = suite->playerProvider.getPlayer();
    bool roleCallbackFired = false;
    player->setOnRoleChanged([&roleCallbackFired]() { roleCallbackFired = true; });

    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");
    appendTestTeam(body, uuidWireBytes(TEST_TEAM_ID), "Bounty");
    appendTestAffiliationNamed(body, "Reboot Syndicate");
    ASSERT_NO_FATAL_FAILURE(registerThroughFdn(suite, body));

    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_STREQ(player->getServerPlayerName(), "Ada");
    EXPECT_FALSE(player->isHunter());
    EXPECT_EQ(player->getAllegiance(), Allegiance::RESISTANCE);
    EXPECT_TRUE(roleCallbackFired) << "applying registration profile must notify connected peers";
    EXPECT_STREQ(player->getUserID().c_str(), "7d1f0c52-3a6e-4f0b-9c1d-2e8a4b6c0d11");
    EXPECT_TRUE(holdsTestRecordKey(*suite->playerProvider.getRecordKey()))
    << "the PDN does not hold the key the server issued";
}

// The totals the server returned for the player reach the PDN's Player unchanged,
// and stand there as received while this PDN's own matches are counted apart.
inline void registrationTakesServerTotalsAsBaseline(PlayerFetchTestSuite* suite) {
    std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");
    appendTestTotals(body, makeTestTotals());
    ASSERT_NO_FATAL_FAILURE(registerThroughFdn(suite, body));

    Player* player = suite->playerProvider.getPlayer();
    ASSERT_TRUE(suite->playerProvider.hasPlayer());
    expectTotalsEqual(player->getRegistrationTotals(), makeTestTotals());
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), makeTestTotals().matchesPlayed)
    << "server totals were counted as session matches";
    EXPECT_EQ(player->getWins(), 0);
    EXPECT_EQ(player->getDisplayedAverageReactionTime(), makeTestTotals().averageDrawTimeMs);

    player->incrementMatchesPlayed();
    player->incrementWins();
    player->addReactionTime(250);
    expectTotalsEqual(player->getRegistrationTotals(), makeTestTotals());
    EXPECT_EQ(player->getDisplayedMatchesPlayed(), makeTestTotals().matchesPlayed + 1u);
    EXPECT_EQ(player->getWins(), 1);
    EXPECT_EQ(
    player->getDisplayedAverageReactionTime(),
    (static_cast<uint64_t>(makeTestTotals().averageDrawTimeMs) * makeTestTotals().matchesPlayed +
     250u) /
    (makeTestTotals().matchesPlayed + 1u));

    suite->playerProvider.clear();
    expectTotalsEqual(player->getRegistrationTotals(), PlayerTotals{});
}

inline void registrationWithoutServerTotalsHasZeroBaseline(PlayerFetchTestSuite* suite) {
    const std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");
    ASSERT_NO_FATAL_FAILURE(registerThroughFdn(suite, body));

    ASSERT_TRUE(suite->playerProvider.hasPlayer()) << "a lookup with no totals must still register";
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Ada");
    expectTotalsEqual(suite->playerProvider.getPlayer()->getRegistrationTotals(), PlayerTotals{});
}

inline void provisionPacketCarriesTotals() {
    Player source;
    source.applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID), "Ada", Team{}, {},
                                    makeTestTotals());
    const PlayerProvisionPacket sent = PlayerProvisionCodec::encode(source);
    Player received;
    ASSERT_TRUE(PlayerProvisionCodec::decode(sent, received));
    expectTotalsEqual(received.getRegistrationTotals(), makeTestTotals());

    // A packet is whatever its sender says; totals with wins above matches are
    // dropped and the profile still applies.
    PlayerProvisionPacket impossible = sent;
    impossible.totalWins = impossible.totalMatchesPlayed + 1;
    Player other;
    ASSERT_TRUE(PlayerProvisionCodec::decode(impossible, other));
    EXPECT_STREQ(other.getServerPlayerName(), "Ada");
    expectTotalsEqual(other.getRegistrationTotals(), PlayerTotals{});
}

inline void awaitBandIgnoresUncabledOrMalformedProvision(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);

    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    // Checked on the next tick, before the clock moves: a failure page that had
    // already timed out would read the same as one never shown.
    suite->deliverProvision(makeLookupFailedPacket(), strangerMac);
    EXPECT_FALSE(suite->failurePageShowsAfterTick(awaitBand))
    << "a failure from a sender not Connected on a jack showed the failure page";

    // An attempt the profile started would time out onto the failure page on this tick.
    const PlayerProvisionPacket spoofed = makeProvisionPacket("Spoofed");
    suite->deliverProvision(spoofed, strangerMac);
    suite->fakeClock->advance(REGISTRATION_ATTEMPT_TIMEOUT_MS + 1);
    EXPECT_FALSE(suite->failurePageShowsAfterTick(awaitBand))
    << "a sender not Connected on a jack started an attempt";

    // With a key in hand, only the packet checks stand between these and the player.
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    ASSERT_TRUE(suite->playerProvider.getRecordKey()->isHeld());
    suite->deliverProvision(spoofed, strangerMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());

    PlayerProvisionPacket unnamed = spoofed;
    unnamed.playerName[0] = '\0';
    suite->deliverProvision(unnamed, suite->fdnMac);
    suite->device.mockPeerComms->triggerPacketCallback(suite->fdnMac, PktType::kPlayerProvision,
                                                       reinterpret_cast<const uint8_t*>(&spoofed),
                                                       sizeof(spoofed) - 1);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
}

// The failure page belongs to the waiting state: it holds for its full time against
// cable changes and late pieces, then the waiting screen returns with the session
// cleared and a fresh attempt open.
inline void awaitBandFailurePageReturnsToWaiting(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    ASSERT_TRUE(suite->lastScreenReads("Tap band", "at FDN"));

    suite->deliverProvision(makeLookupFailedPacket(), suite->fdnMac);
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand));

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld()) << "a key landed on the page";
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "a profile landed on the page";
    suite->cableFdn(false);
    suite->fakeClock->advance(REGISTRATION_FAILURE_PAGE_TIMEOUT_MS - 1);
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand))
    << "the cable redraw or an early timeout replaced the page";

    // Whatever the session holds when the page ends does not outlive it.
    suite->playerProvider.getPlayer()->applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID),
                                                                "Stale", Team{}, {});
    ASSERT_TRUE(suite->playerProvider.hasPlayer());
    suite->fakeClock->advance(2);
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Cable to", "FDN"));
    EXPECT_FALSE(suite->playerProvider.hasPlayer());

    suite->cableFdn(true);
    awaitBand.onStateLoop(&suite->device);
    EXPECT_TRUE(suite->lastScreenReads("Tap band", "at FDN"));
    const ApplicationPayload key = makeTestRecordKey(0xB0);
    suite->deliverRecordKey(key);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_TRUE(awaitBand.transitionToWelcome()) << "no fresh attempt opened after the page";
    EXPECT_TRUE(suite->holdsRecordKey(key));
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandRegistersOnKeyThenProfile(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);

    const ApplicationPayload key = makeTestRecordKey(0xA0);
    suite->deliverRecordKey(key);
    EXPECT_FALSE(awaitBand.transitionToWelcome()) << "a key alone registers nobody";
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);

    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_TRUE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
    EXPECT_TRUE(suite->holdsRecordKey(key)) << "the key serves the session after registration";
}

// The key crosses the jack and the profile the radio, so the profile can win the race.
inline void awaitBandRegistersOnProfileThenKey(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);

    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_FALSE(suite->failurePageShowsAfterTick(awaitBand));
    EXPECT_FALSE(suite->playerProvider.hasPlayer()) << "a profile is not applied without a key";
    EXPECT_FALSE(awaitBand.transitionToWelcome());

    const ApplicationPayload key = makeTestRecordKey(0xA0);
    suite->deliverRecordKey(key);
    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Ada");
    EXPECT_TRUE(awaitBand.transitionToWelcome());
    EXPECT_TRUE(suite->holdsRecordKey(key));
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandProfileWithoutKeyFailsAtTimeLimit(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    suite->fakeClock->advance(REGISTRATION_ATTEMPT_TIMEOUT_MS - 1);
    EXPECT_FALSE(suite->failurePageShowsAfterTick(awaitBand));
    suite->fakeClock->advance(2);
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand));

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer())
    << "a key after the attempt failed must not revive its profile";
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    EXPECT_FALSE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandKeyWithoutProfileIsClearedAtTimeLimit(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    ASSERT_TRUE(suite->playerProvider.getRecordKey()->isHeld());

    suite->fakeClock->advance(REGISTRATION_ATTEMPT_TIMEOUT_MS + 1);
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand));
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());

    suite->deliverProvision(makeProvisionPacket("Late"), suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandLookupFailureFailsAttempt(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    suite->deliverProvision(makeLookupFailedPacket(), suite->fdnMac);
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand))
    << "a lookup failure from the cabled FDN shows the failure page";
    awaitBand.onStateDismounted(&suite->device);

    awaitBand.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverProvision(makeLookupFailedPacket(), suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "the key outlived the registration that failed";
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand));
    awaitBand.onStateDismounted(&suite->device);

    awaitBand.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    suite->deliverProvision(makeLookupFailedPacket(), suite->fdnMac);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    EXPECT_FALSE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandAcceptsKeyOnlyFromAJackWithConnectedFdn(PlayerFetchTestSuite* suite) {
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    suite->cableFdn(false);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);

    suite->deliverRecordKey(makeTestRecordKey(0x10));
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld()) << "no FDN is Connected";

    suite->cableFdn(true);
    awaitBand.onStateLoop(&suite->device);

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

    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());

    const ApplicationPayload key = makeTestRecordKey(0x50);
    suite->deliverRecordKey(key);
    EXPECT_TRUE(suite->holdsRecordKey(key));
    EXPECT_TRUE(suite->playerProvider.hasPlayer());
    awaitBand.onStateDismounted(&suite->device);
}

// With an FDN on each jack, the first piece binds the attempt to its FDN.
inline void awaitBandTakesBothPiecesFromOneFdn(PlayerFetchTestSuite* suite) {
    const uint8_t otherFdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x02};
    suite->cableOtherFdnOnOutputJack(otherFdnMac);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverProvision(makeProvisionPacket("Other"), otherFdnMac);
    EXPECT_FALSE(suite->playerProvider.hasPlayer())
    << "a profile from an FDN other than the key's was applied";
    suite->deliverProvision(makeLookupFailedPacket(), otherFdnMac);
    EXPECT_FALSE(suite->failurePageShowsAfterTick(awaitBand));
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_STREQ(suite->playerProvider.getPlayer()->getServerPlayerName(), "Ada");
    awaitBand.onStateDismounted(&suite->device);

    awaitBand.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    suite->deliverRecordKey(makeTestRecordKey(0xB0), SerialIdentifier::OUTPUT_JACK);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "a key over the other FDN's jack was taken";
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    const ApplicationPayload key = makeTestRecordKey(0xC0);
    suite->deliverRecordKey(key);
    EXPECT_TRUE(suite->holdsRecordKey(key));
    EXPECT_TRUE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandLaterKeyReplacesHeldKey(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);

    const ApplicationPayload later = makeTestRecordKey(0x60);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverRecordKey(later);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    EXPECT_TRUE(suite->holdsRecordKey(later));

    // Registration is complete: a frame after it belongs to no attempt.
    suite->deliverRecordKey(makeTestRecordKey(0x10));
    EXPECT_TRUE(suite->holdsRecordKey(later));
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandJackLossAfterKeyFailsAttempt(PlayerFetchTestSuite* suite) {
    const uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    FakeRemoteDeviceCoordinator& rdc = suite->device.fakeRemoteDeviceCoordinator;
    rdc.setPeerDeviceType(SerialIdentifier::OUTPUT_JACK, DeviceType::PDN);
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, peerMac);
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::CONNECTED);
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);

    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    rdc.setJackStatus(SerialIdentifier::OUTPUT_JACK, JackStatus::DISCONNECTED);
    ASSERT_TRUE(suite->playerProvider.getRecordKey()->isHeld())
    << "a peer leaving the other jack is no concern of the registration";

    // Well inside the attempt window: the failure does not wait for it.
    suite->cableFdn(false);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand));
    suite->cableFdn(true);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer())
    << "the attempt ended when the FDN's jack dropped";
    EXPECT_FALSE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandJackLossWithProfileHeldFailsAttempt(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);

    suite->cableFdn(false);
    EXPECT_TRUE(suite->failurePageShowsAfterTick(awaitBand));
    suite->cableFdn(true);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(suite->playerProvider.hasPlayer())
    << "the attempt ended when the FDN's jack dropped";
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());
    awaitBand.onStateDismounted(&suite->device);
}

inline void awaitBandLeavingAttemptDropsKeyAndProfile(PlayerFetchTestSuite* suite) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);

    // Left mid-attempt, as when a hold switches to firmware seeding before the FDN answers.
    awaitBand.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    awaitBand.onStateDismounted(&suite->device);
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld());

    awaitBand.onStateMounted(&suite->device);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    awaitBand.onStateDismounted(&suite->device);
    awaitBand.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    EXPECT_FALSE(awaitBand.transitionToWelcome()) << "a profile outlived the attempt it answered";
    awaitBand.onStateDismounted(&suite->device);
}

// Each showing of the waiting screen starts from an empty session, so a failed or
// earlier registration leaves nothing behind.
inline void awaitBandMountClearsTheSession(PlayerFetchTestSuite* suite) {
    Player* player = suite->playerProvider.getPlayer();
    player->applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID), "Ada", Team{}, {},
                                     makeTestTotals());
    suite->playerProvider.getRecordKey()->set(makeTestRecordKey(0xA0).data());
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    EXPECT_FALSE(suite->playerProvider.hasPlayer());
    EXPECT_TRUE(player->getServerPlayerId().isZero());
    EXPECT_FALSE(suite->playerProvider.getRecordKey()->isHeld())
    << "showing the waiting screen kept the last registration's key";
    expectTotalsEqual(player->getRegistrationTotals(), PlayerTotals{});
    awaitBand.onStateDismounted(&suite->device);
}

/** Registers on a fresh waiting screen as @p playerIdText and returns the next rand() values. */
inline std::vector<int> randAfterRegistering(PlayerFetchTestSuite* suite,
                                             const char* playerIdText) {
    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    suite->deliverRecordKey(makeTestRecordKey(0xA0));
    suite->deliverProvision(makeProvisionPacket("Ada", playerIdText), suite->fdnMac);
    EXPECT_TRUE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);
    std::vector<int> values;
    for (int i = 0; i < 4; ++i) {
        values.push_back(std::rand());
    }
    return values;
}

inline void registrationSeedsRandomFromPlayerId(PlayerFetchTestSuite* suite) {
    const std::vector<int> first = randAfterRegistering(suite, TEST_PLAYER_ID);
    std::srand(12345);
    const std::vector<int> again = randAfterRegistering(suite, TEST_PLAYER_ID);
    EXPECT_EQ(first, again) << "the same player did not get the same seed";
    const std::vector<int> other =
    randAfterRegistering(suite, "11111111-2222-4000-8000-333333333333");
    EXPECT_NE(first, other) << "the seed does not depend on the player id";
}

// The demo profile has no server behind it, so it starts from no totals even on
// a Player that held some.
inline void chooseTeamConfirmEntersHub(PlayerFetchTestSuite* suite) {
    Player* player = suite->playerProvider.getPlayer();
    player->applyRegistrationProfile(makeTestUuid(TEST_PLAYER_ID), "Ada", Team{}, {},
                                     makeTestTotals());
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
    EXPECT_STREQ(player->getServerPlayerName(), "Demo");
    EXPECT_TRUE(player->getServerPlayerId().isZero()) << "a demo player has no server identity";
    expectTotalsEqual(player->getRegistrationTotals(), PlayerTotals{});
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

    // A demo Hunter keeps the default role but still tells peers it is registered,
    // so an FDN queues no registration for it.
    Player demo{};
    int demoNotifications = 0;
    demo.setOnRoleChanged([&demoNotifications]() { demoNotifications++; });
    Team hunter{};
    strncpy(hunter.name, "Hunter", sizeof(hunter.name) - 1);
    demo.applyRegistrationProfile(UUIDBytes(), "Demo", hunter, {});
    EXPECT_EQ(demoNotifications, 1);
    EXPECT_EQ(demo.toProfile().registered, 1);
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

    AwaitBandState awaitBand(suite->device.wirelessManager, &suite->playerProvider);
    awaitBand.onStateMounted(&suite->device);
    const ApplicationPayload key = makeTestRecordKey(0xA0);
    suite->deliverRecordKey(key);
    suite->deliverProvision(makeProvisionPacket("Ada"), suite->fdnMac);
    awaitBand.onStateLoop(&suite->device);
    ASSERT_TRUE(awaitBand.transitionToWelcome());
    awaitBand.onStateDismounted(&suite->device);

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
