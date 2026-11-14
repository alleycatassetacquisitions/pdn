#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "game/match-manager.hpp"
#include "game/player.hpp"
#include "device-mock.hpp"
#include "utility-tests.hpp"

// X-3: a standalone match's results are refused before the draw window opens,
// and its frames are signed under a key the Hunter sends over the cable.
// Two managers, each with its own radio tap and fake coordinator; the test
// plays the radio and the cable between them.
class MatchFrameAuthTests : public testing::Test {
public:
    static constexpr unsigned long START_TIME_MS = 20000;

    /** Builds a cabled Hunter and Bounty pair on a fake clock. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(START_TIME_MS);

        hunter = new Player();
        char hunterId[] = "hunt";
        hunter->setUserID(hunterId);
        hunter->setIsHunter(true);
        bounty = new Player();
        char bountyId[] = "boun";
        bounty->setUserID(bountyId);
        bounty->setIsHunter(false);

        // The cable: hunter's output jack to the bounty's input jack.
        connectPdnPeer(hunterRdc, SerialIdentifier::OUTPUT_JACK, bountyMac);
        connectPdnPeer(bountyRdc, SerialIdentifier::INPUT_JACK, hunterMac);

        hunterRadio.attach(hunterDevice.mockPeerComms);
        bountyRadio.attach(bountyDevice.mockPeerComms);
        hunterMatchManager =
        new MatchManager(hunter, nullptr, &hunterStorage, hunterDevice.wirelessManager, &hunterRdc);
        bountyMatchManager =
        new MatchManager(bounty, nullptr, &bountyStorage, bountyDevice.wirelessManager, &bountyRdc);
    }

    /** Releases both match managers and the fake clock. */
    void TearDown() override {
        delete hunterMatchManager;
        delete bountyMatchManager;
        delete hunter;
        delete bounty;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** The Hunter's initiation (the frame its radio last sent) reaches the bounty. */
    void deliverInitiationToBounty() {
        ASSERT_NE(hunterRadio.lastFrame(), nullptr);
        ASSERT_EQ(hunterRadio.lastFrame()->command, MatchCmd::SEND_MATCH_ID);
        bountyRadio.deliverFrameFrom(hunterMac, hunterRadio.lastFrame());
    }

    /** The full handshake, key first, with the bounty's ack delivered and confirmed. */
    void performHandshake() {
        hunterMatchManager->initializeMatch(bountyMac);
        carryMatchKeyAcrossJack(hunterRdc, bountyRdc);
        deliverInitiationToBounty();
        ASSERT_TRUE(bountyMatchManager->getCurrentMatch().has_value())
        << "the bounty primed no match";
        hunterRadio.deliverFrameFrom(bountyMac, bountyRadio.lastFrame());
        bountyRadio.confirmLastSend();
        ASSERT_TRUE(hunterMatchManager->isMatchReady());
        ASSERT_TRUE(bountyMatchManager->isMatchReady());
    }

    /** Both draw windows open now. */
    void openBothWindows() {
        hunterMatchManager->setMatchLocalStartTime(fakeClock->milliseconds());
        bountyMatchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    }

    /** The Hunter presses `reactionMs` after its window opened; returns the DRAW_RESULT it sent. */
    MatchPacket hunterPressesAfter(unsigned long reactionMs) {
        fakeClock->setTime(hunterMatchManager->getMatchLocalStartTime() + reactionMs);
        hunterMatchManager->getMatchButtonPush()(hunterMatchManager);
        EXPECT_NE(hunterRadio.lastFrame(), nullptr);
        EXPECT_EQ(hunterRadio.lastFrame()->command, MatchCmd::DRAW_RESULT);
        return *hunterRadio.lastFrame();
    }

    /** @return the match id the Hunter is playing. */
    const char* hunterMatchId() const {
        return hunterMatchManager->getCurrentMatch()->getMatchId();
    }

    /** True when the frame @p radio last sent carries no counter and an all-zero tag. */
    static bool lastFrameIsUnsigned(const QuickdrawRadioTap& radio) {
        const MatchPacket* frame = radio.lastFrame();
        if (frame == nullptr) return false;
        const uint8_t zeroTag[MATCH_TAG_SIZE] = {};
        return frame->counter == 0 && memcmp(frame->tag, zeroTag, MATCH_TAG_SIZE) == 0;
    }

    uint8_t hunterMac[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    uint8_t bountyMac[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
    FakePlatformClock* fakeClock = nullptr;
    Player* hunter = nullptr;
    Player* bounty = nullptr;
    MockDevice hunterDevice;
    MockDevice bountyDevice;
    QuickdrawRadioTap hunterRadio;
    QuickdrawRadioTap bountyRadio;
    testing::NiceMock<MockStorage> hunterStorage;
    testing::NiceMock<MockStorage> bountyStorage;
    FakeRemoteDeviceCoordinator hunterRdc;
    FakeRemoteDeviceCoordinator bountyRdc;
    MatchManager* hunterMatchManager = nullptr;
    MatchManager* bountyMatchManager = nullptr;
};

// ---- (a) Sanity checks ----

// Catches: a result taken during the countdown, which only a guess or a forgery can produce.
TEST_F(MatchFrameAuthTests, drawResultBeforeTheWindowOpensIsDropped) {
    performHandshake();
    hunterMatchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    const MatchPacket result = hunterPressesAfter(200);

    bountyRadio.deliverFrameFrom(hunterMac, &result);

    EXPECT_FALSE(bountyMatchManager->getHasReceivedDrawResult());
    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 0u);
}

// Catches: the window gate applied to draw results only, not to never-pressed reports.
TEST_F(MatchFrameAuthTests, neverPressedBeforeTheWindowOpensIsDropped) {
    performHandshake();
    hunterMatchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    hunterMatchManager->sendNeverPressed(900);

    bountyRadio.deliverFrameFrom(hunterMac, hunterRadio.lastFrame());

    EXPECT_FALSE(bountyMatchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(bountyMatchManager->matchResultsAreIn());
}

// Catches: a reaction-time floor. The countdown is a fixed rhythm, so an honest
// press that anticipates the draw can land at or near 0 ms and must score.
TEST_F(MatchFrameAuthTests, nearZeroHonestPressInAStandaloneMatchIsScored) {
    performHandshake();
    openBothWindows();

    const MatchPacket hunterResult = hunterPressesAfter(3);
    bountyRadio.deliverFrameFrom(hunterMac, &hunterResult);

    ASSERT_TRUE(bountyMatchManager->getHasReceivedDrawResult()) << "a 3 ms raw time was refused";
    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 3u);
    bountyMatchManager->sendNeverPressed(750);
    hunterRadio.deliverFrameFrom(bountyMac, bountyRadio.lastFrame());
    EXPECT_TRUE(hunterMatchManager->didWin());
    EXPECT_FALSE(bountyMatchManager->didWin());
}

// Catches: a check on the effective time, which would refuse an honest fast draw
// behind a big posse; and the pity report refused.
TEST_F(MatchFrameAuthTests, honestFastDrawWithBoostAndAPityReportAreAccepted) {
    performHandshake();
    openBothWindows();
    hunterMatchManager->setBoostProvider([]() -> unsigned long { return 120; });

    const MatchPacket hunterResult = hunterPressesAfter(90);
    bountyRadio.deliverFrameFrom(hunterMac, &hunterResult);
    ASSERT_TRUE(bountyMatchManager->getHasReceivedDrawResult());
    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 90u);

    // The bounty misses its grace window and reports its pity time.
    bountyMatchManager->sendNeverPressed(750);
    hunterRadio.deliverFrameFrom(bountyMac, bountyRadio.lastFrame());
    EXPECT_TRUE(hunterMatchManager->getHasReceivedDrawResult());
    EXPECT_TRUE(hunterMatchManager->didWin());
    EXPECT_FALSE(bountyMatchManager->didWin());
}

// Catches: the spec's boost scenario (raws 90 and 100, 120 ms boost) broken by a draw check.
TEST_F(MatchFrameAuthTests, boostedNinetyBeatsAnUnboostedHundredOnBothDevices) {
    performHandshake();
    openBothWindows();
    hunterMatchManager->setBoostProvider([]() -> unsigned long { return 120; });

    const MatchPacket hunterResult = hunterPressesAfter(90);
    fakeClock->setTime(bountyMatchManager->getMatchLocalStartTime() + 100);
    bountyMatchManager->getMatchButtonPush()(bountyMatchManager);
    const MatchPacket bountyResult = *bountyRadio.lastFrame();
    ASSERT_EQ(bountyResult.command, MatchCmd::DRAW_RESULT);

    bountyRadio.deliverFrameFrom(hunterMac, &hunterResult);
    hunterRadio.deliverFrameFrom(bountyMac, &bountyResult);

    ASSERT_TRUE(hunterMatchManager->matchResultsAreIn());
    ASSERT_TRUE(bountyMatchManager->matchResultsAreIn());
    EXPECT_TRUE(hunterMatchManager->didWin()) << "90 - 120 = -30 beats 100";
    EXPECT_FALSE(bountyMatchManager->didWin());
    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 90u);
}

// Catches: the draw-window check applied to shootout matches, whose windows open a retry apart.
TEST_F(MatchFrameAuthTests, shootoutResultAtTenMillisecondsIsStillAcceptedUnsigned) {
    const char shootoutId[] = "SHT-00000000000000000000000000000042";
    bountyMatchManager->initializeShootoutMatch(shootoutId, hunterMac, /*localIsHunter=*/false);

    MatchPacket result{};
    IdGenerator::copyId(result.matchId, shootoutId);
    result.command = MatchCmd::DRAW_RESULT;
    result.isHunter = 1;
    result.playerDrawTime = 10;
    result.seqId = 7;
    bountyRadio.deliverFrameFrom(hunterMac, &result);

    EXPECT_TRUE(bountyMatchManager->getHasReceivedDrawResult());
    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 10u);
    bountyMatchManager->sendNeverPressed(750);
    EXPECT_TRUE(lastFrameIsUnsigned(bountyRadio)) << "a shootout match holds no key";
}

// ---- (b) Match key over the jack ----

// Catches: the key sent after the initiation, on the wrong jack, or not the key that signs.
TEST_F(MatchFrameAuthTests, hunterQueuesTheKeyOnItsOpponentJackBeforeTheSignedInitiation) {
    hunterMatchManager->initializeMatch(bountyMac);

    const FakeRemoteDeviceCoordinator::SentApplicationFrame* keyFrame =
    lastSentMatchKeyFrame(hunterRdc);
    ASSERT_NE(keyFrame, nullptr) << "no match key was queued";
    EXPECT_EQ(keyFrame->jack, SerialIdentifier::OUTPUT_JACK);
    UUIDBytes namedMatch{};
    ASSERT_TRUE(UUIDBytes::parse(hunterMatchId(), namedMatch));
    EXPECT_EQ(0, memcmp(keyFrame->payload.data(), namedMatch.bytes, UUIDBytes::SIZE));

    MatchKey key{};
    ASSERT_TRUE(lastSentMatchKey(hunterRdc, key));
    ASSERT_EQ(hunterRadio.sentFrames.size(), 1u);
    const MatchPacket& initiation = hunterRadio.sentFrames[0];
    EXPECT_EQ(initiation.command, MatchCmd::SEND_MATCH_ID);
    EXPECT_EQ(initiation.counter, 1u);
    EXPECT_TRUE(matchPacketTagVerifies(key, initiation));
}

// Catches: a match started, and an initiation sent, after the jack refused the key.
TEST_F(MatchFrameAuthTests, refusedKeyFrameStartsNoMatch) {
    hunterRdc.setApplicationFramesRefused(true);

    hunterMatchManager->initializeMatch(bountyMac);

    EXPECT_FALSE(hunterMatchManager->getCurrentMatch().has_value());
    EXPECT_TRUE(hunterRadio.sentFrames.empty()) << "an initiation went out with no key behind it";
}

// Catches: the serial frame trailing the radio frame losing the match.
TEST_F(MatchFrameAuthTests, initiationHeldUntilItsKeyArrives) {
    hunterMatchManager->initializeMatch(bountyMac);
    deliverInitiationToBounty();
    EXPECT_FALSE(bountyMatchManager->getCurrentMatch().has_value()) << "primed with no key";

    carryMatchKeyAcrossJack(hunterRdc, bountyRdc);

    ASSERT_TRUE(bountyMatchManager->getCurrentMatch().has_value());
    EXPECT_STREQ(bountyMatchManager->getCurrentMatch()->getMatchId(), hunterMatchId());
    MatchKey key{};
    ASSERT_TRUE(lastSentMatchKey(hunterRdc, key));
    ASSERT_NE(bountyRadio.lastFrame(), nullptr);
    EXPECT_EQ(bountyRadio.lastFrame()->command, MatchCmd::MATCH_ID_ACK);
    EXPECT_TRUE(matchPacketTagVerifies(key, *bountyRadio.lastFrame()))
    << "the bounty does not sign under the key it paired";
}

// Catches: a bounty that primes on an initiation when the cable never carried a key.
TEST_F(MatchFrameAuthTests, noKeyOnTheCablePrimesNoMatch) {
    hunterMatchManager->initializeMatch(bountyMac);
    deliverInitiationToBounty();

    EXPECT_FALSE(bountyMatchManager->getCurrentMatch().has_value());
    EXPECT_FALSE(hunterMatchManager->isMatchReady());
}

// Catches: a key dropped because the bounty's link was still connecting when the
// Hunter's, Connected first, sent it; the match then waited for Idle's retry.
TEST_F(MatchFrameAuthTests, keyArrivingWhileTheLinkIsConnectingPairsOnceItConnects) {
    bountyRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTING);
    hunterMatchManager->initializeMatch(bountyMac);
    carryMatchKeyAcrossJack(hunterRdc, bountyRdc);
    deliverInitiationToBounty();
    EXPECT_FALSE(bountyMatchManager->getCurrentMatch().has_value())
    << "primed before the opponent jack was Connected";

    bountyRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);

    ASSERT_TRUE(bountyMatchManager->getCurrentMatch().has_value()) << "the held key was dropped";
    EXPECT_STREQ(bountyMatchManager->getCurrentMatch()->getMatchId(), hunterMatchId());
}

// Catches: a key taken from a jack other than the opponent jack.
TEST_F(MatchFrameAuthTests, keyFromAnotherJackIsIgnored) {
    connectPdnPeer(bountyRdc, SerialIdentifier::OUTPUT_JACK, hunterMac);
    hunterMatchManager->initializeMatch(bountyMac);

    carryMatchKeyAcrossJack(hunterRdc, bountyRdc, SerialIdentifier::OUTPUT_JACK,
                            SerialIdentifier::OUTPUT_JACK);
    deliverInitiationToBounty();

    EXPECT_FALSE(bountyMatchManager->getCurrentMatch().has_value());
}

// Catches: the match observer taking a record-key frame (routing by payload, not opcode).
TEST_F(MatchFrameAuthTests, recordKeyFrameDoesNotReachTheMatchKeyObserver) {
    hunterMatchManager->initializeMatch(bountyMac);
    const FakeRemoteDeviceCoordinator::SentApplicationFrame* keyFrame =
    lastSentMatchKeyFrame(hunterRdc);
    ASSERT_NE(keyFrame, nullptr);

    bountyRdc.deliverApplicationFrame(SerialIdentifier::INPUT_JACK,
                                      ApplicationFrameKind::RECORD_KEY, keyFrame->payload);
    deliverInitiationToBounty();

    EXPECT_FALSE(bountyMatchManager->getCurrentMatch().has_value());
}

// Catches: a forged initiation for the keyed match id priming a match.
TEST_F(MatchFrameAuthTests, initiationWhoseTagFailsPrimesNoMatchAndTheGenuineOneStillDoes) {
    hunterMatchManager->initializeMatch(bountyMac);
    carryMatchKeyAcrossJack(hunterRdc, bountyRdc);

    MatchPacket forged = *hunterRadio.lastFrame();
    forged.tag[0] ^= 0xFF;
    forged.seqId = static_cast<uint8_t>(forged.seqId + 1);
    bountyRadio.deliverFrameFrom(hunterMac, &forged);
    EXPECT_FALSE(bountyMatchManager->getCurrentMatch().has_value());

    deliverInitiationToBounty();
    EXPECT_TRUE(bountyMatchManager->getCurrentMatch().has_value())
    << "the key was thrown away with the forged initiation";
}

// Catches: a key held past a cable pull, to pair with an initiation after the reseat.
TEST_F(MatchFrameAuthTests, cableDropWipesAHeldKey) {
    hunterMatchManager->initializeMatch(bountyMac);
    carryMatchKeyAcrossJack(hunterRdc, bountyRdc);

    bountyRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    bountyRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    deliverInitiationToBounty();

    EXPECT_FALSE(bountyMatchManager->getCurrentMatch().has_value());
}

// Catches: an opponent-jack blip during a match wiping the match key, so the
// opponent's signed result is then dropped.
TEST_F(MatchFrameAuthTests, aReseatedCableKeepsTheMatchKey) {
    performHandshake();
    bountyRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    bountyRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);

    openBothWindows();
    const MatchPacket genuine = hunterPressesAfter(250);
    bountyRadio.deliverFrameFrom(hunterMac, &genuine);

    EXPECT_TRUE(bountyMatchManager->getHasReceivedDrawResult())
    << "a reseated blip cost the match its key";
}

// Catches: a key that outlives its match on either device: the next match each
// plays, a shootout that holds no key, would go out signed.
TEST_F(MatchFrameAuthTests, keyIsWipedWhenTheMatchIsFinalizedOrCleared) {
    const char shootoutId[] = "SHT-00000000000000000000000000000042";
    performHandshake();

    hunterMatchManager->setHunterDrawTime(200);
    hunterMatchManager->setBountyDrawTime(300);
    hunterMatchManager->setReceivedButtonPush();
    hunterMatchManager->setReceivedDrawResult();
    ASSERT_TRUE(hunterMatchManager->finalizeMatch());
    hunterMatchManager->initializeShootoutMatch(shootoutId, bountyMac, /*localIsHunter=*/true);
    hunterMatchManager->sendNeverPressed(750);
    EXPECT_TRUE(lastFrameIsUnsigned(hunterRadio)) << "finalize kept the key";

    bountyMatchManager->clearCurrentMatch();
    bountyMatchManager->initializeShootoutMatch(shootoutId, hunterMac, /*localIsHunter=*/false);
    bountyMatchManager->sendNeverPressed(750);
    EXPECT_TRUE(lastFrameIsUnsigned(bountyRadio)) << "clear kept the key";
}

// ---- (b) Signed standalone frames ----

// Catches: verification after dedupe, where a forgery under the genuine frame's
// seqId has the genuine result dropped as a retransmit; and a missing tag accepted.
TEST_F(MatchFrameAuthTests, forgedResultIsDroppedWithoutTakingTheGenuineFramesSeqId) {
    performHandshake();
    openBothWindows();
    const MatchPacket genuine = hunterPressesAfter(250);

    MatchPacket wrongKey = genuine;
    wrongKey.playerDrawTime = 80;
    wrongKey.boostMs = 270;
    signMatchFrame(wrongKey, testMatchKey(0x40), genuine.counter);
    wrongKey.seqId = genuine.seqId;
    bountyRadio.deliverFrameFrom(hunterMac, &wrongKey);

    MatchPacket untagged = wrongKey;
    memset(untagged.tag, 0, sizeof(untagged.tag));
    untagged.counter = 0;
    bountyRadio.deliverFrameFrom(hunterMac, &untagged);
    EXPECT_FALSE(bountyMatchManager->getHasReceivedDrawResult()) << "a forged result was taken";

    bountyRadio.deliverFrameFrom(hunterMac, &genuine);
    ASSERT_TRUE(bountyMatchManager->getHasReceivedDrawResult())
    << "the genuine result was dropped as a duplicate of the forgery";
    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 250u);
}

// Catches: a missing counter check, so a captured frame replays under a fresh seqId.
TEST_F(MatchFrameAuthTests, replayedFrameIsDropped) {
    performHandshake();
    openBothWindows();
    const MatchPacket genuine = hunterPressesAfter(250);
    bountyRadio.deliverFrameFrom(hunterMac, &genuine);
    ASSERT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 250u);

    bountyMatchManager->getCurrentMatch()->setHunterDrawTime(0);
    MatchPacket replay = genuine;
    replay.seqId = static_cast<uint8_t>(genuine.seqId + 1);
    bountyRadio.deliverFrameFrom(hunterMac, &replay);

    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 0u)
    << "a replayed frame was taken again";
}

// Catches: a missing role check, so a device's own frame reflected back advances
// its receive counter past the opponent's genuine frame.
TEST_F(MatchFrameAuthTests, reflectedFrameIsDroppedAndDoesNotBurnTheCounter) {
    performHandshake();
    openBothWindows();
    fakeClock->setTime(bountyMatchManager->getMatchLocalStartTime() + 300);
    bountyMatchManager->getMatchButtonPush()(bountyMatchManager);
    MatchPacket reflected = *bountyRadio.lastFrame();
    ASSERT_EQ(reflected.command, MatchCmd::DRAW_RESULT);
    reflected.seqId = 1;
    bountyRadio.deliverFrameFrom(hunterMac, &reflected);

    const MatchPacket genuine = hunterPressesAfter(200);
    ASSERT_EQ(genuine.counter, reflected.counter) << "both sides' second frame";
    bountyRadio.deliverFrameFrom(hunterMac, &genuine);

    EXPECT_TRUE(bountyMatchManager->getHasReceivedDrawResult())
    << "the reflected frame took the counter of the opponent's genuine one";
    EXPECT_EQ(bountyMatchManager->getCurrentMatch()->getHunterDrawTime(), 200u);
}
