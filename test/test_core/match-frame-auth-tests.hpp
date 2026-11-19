#pragma once

#include <gtest/gtest.h>

#include "match-manager-tests.hpp"
#include "utils/hardware-random.hpp"

// ============================================
// Signed standalone match frames (#486)
// ============================================

namespace {

const uint8_t FRAME_AUTH_HUNTER_MAC[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

// Native stand-in for the hardware RNG: bytes counting up from 0xA0.
void countingRandomSource(uint8_t* buffer, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<uint8_t>(0xA0 + i);
    }
}

}  // namespace

// The Hunter draws the secret from the hardware RNG, queues it with the match
// ID on its opponent jack, and signs SEND_MATCH_ID under it as frame 1.
TEST_F(MatchManagerTestSuite, hunterSendsTheSecretOnTheOpponentJackAndSignsTheInitiation) {
    setHardwareRandomSource(countingRandomSource);
    setupMatchAsHunter();
    setHardwareRandomSource(nullptr);

    ASSERT_EQ(fakeRdc.sentApplicationFrames.size(), 1u);
    const FakeRemoteDeviceCoordinator::SentApplicationFrame& frame =
    fakeRdc.sentApplicationFrames[0];
    EXPECT_EQ(frame.jack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(frame.kind, ApplicationFrameKind::MATCH_SECRET);
    MatchSecret expected{};
    countingRandomSource(expected.data(), expected.size());
    EXPECT_EQ(frame.payload,
              matchSecretFrame(matchManager->getCurrentMatch()->getMatchId(), expected));

    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->command, MatchCmd::SEND_MATCH_ID);
    EXPECT_EQ(radioTap.lastFrame()->counter, 1u);
    MatchFrameAuth bountySide(expected);
    EXPECT_TRUE(bountySide.admit(*radioTap.lastFrame(), false));
}

// No secret on the cable, no match: nothing could tell the opponent's results from forgeries.
TEST_F(MatchManagerTestSuite, aRefusedSecretStartsNoMatch) {
    fakeRdc.applicationFramesRefused = true;
    setupMatchAsHunter();

    EXPECT_FALSE(matchManager->getCurrentMatch().has_value());
    EXPECT_TRUE(radioTap.sentFrames.empty());
}

// The radio overtakes the serial jack: the initiation waits for its secret,
// and only then is the match primed and acked.
TEST_F(MatchManagerTestSuite, initiationBeforeItsSecretWaitsForIt) {
    player->setIsHunter(false);
    const MatchSecret secret = testMatchSecret(1);
    MatchFrameAuth hunterSide(secret);
    MatchPacket initiation =
    signedMatchFrame(hunterSide, MatchCmd::SEND_MATCH_ID, TEST_MATCH_ID, true);
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &initiation);
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value());
    EXPECT_TRUE(radioTap.sentFrames.empty());

    const std::vector<uint8_t> frame = matchSecretFrame(TEST_MATCH_ID, secret);
    matchManager->onMatchSecretFrame(SerialIdentifier::INPUT_JACK, frame.data(), frame.size());
    ASSERT_TRUE(matchManager->getCurrentMatch().has_value());
    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->command, MatchCmd::MATCH_ID_ACK);
    EXPECT_TRUE(MatchFrameAuth(secret).admit(*radioTap.lastFrame(), true))
    << "the ack is signed under the match secret";
}

// Without the secret for that match an initiation primes nothing: none at
// all, one for another match, one from the wrong jack, or a tag under another secret.
TEST_F(MatchManagerTestSuite, initiationWithoutItsSecretPrimesNoMatch) {
    player->setIsHunter(false);
    const char* otherMatchId = "bbbbbbbb-0000-4000-8000-000000000002";
    const std::vector<uint8_t> otherMatch = matchSecretFrame(otherMatchId, testMatchSecret(1));
    matchManager->onMatchSecretFrame(SerialIdentifier::INPUT_JACK, otherMatch.data(),
                                     otherMatch.size());
    MatchFrameAuth hunterSide(testMatchSecret(1));
    MatchPacket initiation =
    signedMatchFrame(hunterSide, MatchCmd::SEND_MATCH_ID, TEST_MATCH_ID, true);
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &initiation);
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value()) << "secret for another match";

    const std::vector<uint8_t> wrongJack = matchSecretFrame(TEST_MATCH_ID, testMatchSecret(1));
    matchManager->onMatchSecretFrame(SerialIdentifier::OUTPUT_JACK, wrongJack.data(),
                                     wrongJack.size());
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value()) << "secret off the supporter jack";

    const std::vector<uint8_t> otherSecret = matchSecretFrame(TEST_MATCH_ID, testMatchSecret(9));
    matchManager->onMatchSecretFrame(SerialIdentifier::INPUT_JACK, otherSecret.data(),
                                     otherSecret.size());
    EXPECT_FALSE(matchManager->getCurrentMatch().has_value()) << "tag under another secret";
    EXPECT_TRUE(radioTap.sentFrames.empty());
}

// The #486 attack trace: a radio attacker spoofs the Hunter's MAC and sends a
// raw 0 ms DRAW_RESULT with a full boost under the next seqId. It is refused
// without taking that seqId, so the honest result behind it still counts.
TEST_F(MatchManagerTestSuite, forgedDrawResultIsRefusedAndTheHonestOneStillApplies) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());

    MatchFrameAuth attacker(testMatchSecret(0x55));
    MatchPacket forged =
    signedMatchFrame(attacker, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 0, 270);
    forged.counter = 2;
    forged.seqId = 2;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &forged);
    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());

    MatchPacket honest =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 240, 0);
    honest.seqId = 2;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &honest);
    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 240u);
}

// A forged NEVER_PRESSED would hand the victim a win it did not earn.
TEST_F(MatchManagerTestSuite, forgedNeverPressedIsRefused) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    MatchFrameAuth attacker(testMatchSecret(0x55));
    MatchPacket forged =
    signedMatchFrame(attacker, MatchCmd::NEVER_PRESSED, TEST_MATCH_ID, true, 9999, 0);
    forged.counter = 2;
    forged.seqId = 2;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &forged);

    fakeClock->advance(300);
    matchManager->getMatchButtonPush()(matchManager);
    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());
    EXPECT_FALSE(matchManager->matchResultsAreIn());
}

// A genuine frame recorded and sent again under a fresh seqId, after a later
// one, is refused by its counter.
TEST_F(MatchManagerTestSuite, replayedFrameIsRefused) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    MatchPacket first =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 200, 0);
    first.seqId = 5;
    MatchPacket second =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 250, 0);
    second.seqId = 6;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &first);
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &second);

    first.seqId = 7;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &first);
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 250u);
}

// A frame carrying this device's own draw slot is refused even with a good
// tag, as a reflected copy of its own frame has, and takes no counter: the
// Hunter's next frame still counts.
TEST_F(MatchManagerTestSuite, reflectedOwnFrameIsRefused) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    MatchFrameAuth bountySide(testMatchSecret(1));
    MatchPacket reflected =
    signedMatchFrame(bountySide, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, false, 100, 0);
    // Counter 5, above any the Hunter has sent.
    for (int i = 0; i < 4; ++i)
        bountySide.sign(reflected);
    reflected.seqId = 3;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &reflected);
    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());

    MatchPacket honest =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 240, 0);
    honest.seqId = 4;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &honest);
    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
}

// The countdowns start a frame delivery apart: a result before the local draw
// window is held, never refused, and the latest one is applied when it opens.
TEST_F(MatchManagerTestSuite, earlyResultIsHeldAndTheLatestAppliesWhenTheWindowOpens) {
    setupMatchAsBounty();
    MatchPacket early =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 300, 0);
    early.seqId = 5;
    MatchPacket later =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 260, 30);
    later.seqId = 6;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &early);
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &later);
    EXPECT_FALSE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 0u);

    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 260u);
    fakeClock->advance(250);
    matchManager->getMatchButtonPush()(matchManager);
    EXPECT_FALSE(matchManager->didWin()) << "the held 30 ms boost did not count";
}

// Latest wins inside the window too: a draw result after a never-pressed
// report clears it, or the report would still hand this side the win.
TEST_F(MatchManagerTestSuite, aLaterDrawResultClearsAnEarlierNeverPressed) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    MatchPacket neverPressed =
    signedMatchFrame(*hunterAuth, MatchCmd::NEVER_PRESSED, TEST_MATCH_ID, true, 9999, 0);
    neverPressed.seqId = 5;
    MatchPacket drawResult =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 200, 0);
    drawResult.seqId = 6;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &neverPressed);
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &drawResult);

    fakeClock->advance(250);
    matchManager->getMatchButtonPush()(matchManager);
    EXPECT_FALSE(matchManager->didWin()) << "the earlier never-pressed report still counted";
}

// No reaction-time floor: an anticipated press on the rhythmic countdown can
// land at 0 ms, and a signed 0 ms result counts.
TEST_F(MatchManagerTestSuite, zeroMillisecondResultIsAccepted) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    MatchPacket instant =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 0, 0);
    instant.seqId = 5;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &instant);

    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
    fakeClock->advance(1);
    matchManager->getMatchButtonPush()(matchManager);
    EXPECT_FALSE(matchManager->didWin());
}

// Shootout matches are not cabled and carry no secret: their frames stay
// unsigned and apply on arrival, window or not.
TEST_F(MatchManagerTestSuite, shootoutFramesStayUnsignedAndApplyOnArrival) {
    uint8_t opponent[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const char* matchId = "SHT-00000000000000000000000000486";
    matchManager->initializeShootoutMatch(matchId, opponent, false);
    MatchPacket result{};
    IdGenerator::copyId(result.matchId, matchId);
    result.command = MatchCmd::DRAW_RESULT;
    result.isHunter = 1;
    result.playerDrawTime = 220;
    result.seqId = 4;
    radioTap.deliverFrameFrom(opponent, &result);

    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 220u);
    // Shootout frames still go through duplicate detection.
    MatchPacket retransmit = result;
    retransmit.playerDrawTime = 300;
    radioTap.deliverFrameFrom(opponent, &retransmit);
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 220u)
    << "a shootout frame under a seen seqId was applied";
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    fakeClock->advance(200);
    matchManager->getMatchButtonPush()(matchManager);
    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->counter, 0u) << "a shootout frame went out signed";
}

// A radio attacker spoofs the Hunter's MAC and sends an unsigned frame under
// the next seqId: a role mismatch for the running match, or one naming a
// shootout match. Neither may take that seqId from the honest result behind it.
TEST_F(MatchManagerTestSuite, unsignedFramesDoNotTakeTheSignedResultsSeqId) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());

    MatchPacket mismatch{};
    IdGenerator::copyId(mismatch.matchId, TEST_MATCH_ID);
    mismatch.command = MatchCmd::MATCH_ROLE_MISMATCH;
    mismatch.isHunter = 1;
    mismatch.seqId = 2;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &mismatch);
    ASSERT_TRUE(matchManager->isMatchReady()) << "a role mismatch ended a ready match";

    MatchPacket shootout{};
    IdGenerator::copyId(shootout.matchId, "SHT-00000000000000000000000000486");
    shootout.command = MatchCmd::DRAW_RESULT;
    shootout.isHunter = 1;
    shootout.seqId = 2;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &shootout);

    MatchPacket honest =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 240, 0);
    honest.seqId = 2;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &honest);
    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 240u);
}

// The Hunter's seqId runs across every frame it sends to anyone, so a rematch's
// result can carry the seqId of its last result to this Bounty. The counter,
// not that stale record, decides.
TEST_F(MatchManagerTestSuite, aRematchResultIsNotTakenForTheLastMatchsRetransmit) {
    setupMatchAsBounty();
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    MatchPacket first =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, TEST_MATCH_ID, true, 200, 0);
    first.seqId = 7;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &first);
    ASSERT_TRUE(matchManager->getHasReceivedDrawResult());
    matchManager->clearCurrentMatch();

    const char* rematchId = "cccccccc-0000-4000-8000-000000000003";
    setupMatchAsBounty(rematchId);
    matchManager->setMatchLocalStartTime(fakeClock->milliseconds());
    MatchPacket rematch =
    signedMatchFrame(*hunterAuth, MatchCmd::DRAW_RESULT, rematchId, true, 260, 0);
    rematch.seqId = 7;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &rematch);
    EXPECT_TRUE(matchManager->getHasReceivedDrawResult());
    EXPECT_EQ(matchManager->getCurrentMatch()->getHunterDrawTime(), 260u);
}

// Only the opponent jack's peer can know the secret, so an initiation from the
// supporter jack's peer does not displace the Hunter's while it waits.
TEST_F(MatchManagerTestSuite, supporterPeerInitiationDoesNotDisplaceTheHuntersOne) {
    player->setIsHunter(false);
    const uint8_t supporterMac[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    fakeRdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, supporterMac);
    const MatchSecret secret = testMatchSecret(1);
    MatchFrameAuth hunterSide(secret);
    MatchPacket initiation =
    signedMatchFrame(hunterSide, MatchCmd::SEND_MATCH_ID, TEST_MATCH_ID, true);
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &initiation);
    MatchFrameAuth supporterSide(testMatchSecret(9));
    MatchPacket intrusion =
    signedMatchFrame(supporterSide, MatchCmd::SEND_MATCH_ID, TEST_MATCH_ID, true);
    radioTap.deliverFrameFrom(supporterMac, &intrusion);

    const std::vector<uint8_t> frame = matchSecretFrame(TEST_MATCH_ID, secret);
    matchManager->onMatchSecretFrame(SerialIdentifier::INPUT_JACK, frame.data(), frame.size());
    EXPECT_TRUE(matchManager->getCurrentMatch().has_value());
}

// A running match answers no initiation: a same-role one from its own
// opponent gets no role mismatch, which would go out signed for this match.
TEST_F(MatchManagerTestSuite, aRunningMatchAnswersNoInitiation) {
    setupMatchAsBounty();
    MatchPacket sameRole{};
    IdGenerator::copyId(sameRole.matchId, "dddddddd-0000-4000-8000-000000000004");
    sameRole.command = MatchCmd::SEND_MATCH_ID;
    sameRole.isHunter = 0;
    sameRole.seqId = 9;
    radioTap.deliverFrameFrom(FRAME_AUTH_HUNTER_MAC, &sameRole);
    ASSERT_NE(radioTap.lastFrame(), nullptr);
    EXPECT_EQ(radioTap.lastFrame()->command, MatchCmd::MATCH_ID_ACK);
    EXPECT_TRUE(matchManager->isMatchReady());
}
