#pragma once

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <functional>

#include "device-mock.hpp"
#include "node-network-wireless-tests.hpp"
#include "game/node-network/match-manager-v2.hpp"
#include "game/player-provider.hpp"
#include "id-generator.hpp"
#include "match-storage-tests.hpp"
#include "utility-tests.hpp"

inline constexpr int64_t NODE_NETWORK_TEST_INITIATOR_MS = 1700000000000;
inline constexpr int64_t NODE_NETWORK_TEST_RESPONDER_MS = 1700000005000;

inline void registerNodeNetworkTestPlayer(PlayerProvider& provider, const char* playerId,
                                        const char* teamId) {
    Team team;
    if (teamId != nullptr) {
        team.id = makeTestUuid(teamId);
        IdGenerator::copyId(team.name, "Team");
    }
    provider.getPlayer()->applyRegistrationProfile(makeTestUuid(playerId), "Player", team, {});
}

/** One device: a manager wired to fakes, with its own clock, storage, and outbox. */
class NodeNetworkDevice {
public:
    NodeNetworkDevice(const char* playerId, const char* teamId, const uint8_t (&deviceMac)[6],
                    const uint8_t (&peerMac)[6], int64_t unixMs)
        : manager(nullptr) {
        IdGenerator::initialize(4242);
        memcpy(mac.data(), deviceMac, 6);
        memcpy(peer.data(), peerMac, 6);
        clock.setUnixTimeMs(unixMs);
        registerNodeNetworkTestPlayer(provider, playerId, teamId);
        rdc.setPeerMac(SerialIdentifier::INPUT_JACK, peerMac);
        rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, peerMac);
        wireless.setClock(&clock);
        manager = new MatchManagerV2(&provider, &backingStorage, &wireless, &rdc, &clock);
    }

    ~NodeNetworkDevice() {
        delete manager;
        manager = nullptr;
    }

    NodeNetworkDevice(const NodeNetworkDevice&) = delete;
    NodeNetworkDevice& operator=(const NodeNetworkDevice&) = delete;

    /**
     * Delivers every queued outgoing packet from this device to @p other's wireless layer,
     * skipping packets the drop filter rejects.
     */
    bool deliverTo(NodeNetworkDevice& other) {
        bool delivered = false;
        while (!wireless.sent.empty()) {
            const FakeNodeNetworkWirelessManager::SentPacket next = wireless.sent.front();
            wireless.sent.erase(wireless.sent.begin());
            delivered = true;
            if (dropOutgoing && dropOutgoing(next.packet)) {
                continue;
            }
            feedNodeNetworkPacket(other.wireless, mac.data(), next.packet);
        }
        return delivered;
    }

    /** Drops the first outgoing packet carrying @p command; later copies pass. */
    void dropFirst(NodeNetworkCommand command) {
        bool dropped = false;
        dropOutgoing = [command, dropped](const NodeNetworkPacket& packet) mutable {
            if (dropped || packet.command != static_cast<uint8_t>(command)) {
                return false;
            }
            dropped = true;
            return true;
        };
    }

    std::function<bool(const NodeNetworkPacket&)> dropOutgoing;
    FakePlatformClock clock;
    PlayerProvider provider;
    InMemoryStorage backingStorage;
    FakeNodeNetworkWirelessManager wireless;
    FakeRemoteDeviceCoordinator rdc;
    MatchManagerV2* manager;
    std::array<uint8_t, 6> mac = {};
    std::array<uint8_t, 6> peer = {};
};

inline constexpr uint8_t NODE_NETWORK_TEST_MAC_A[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0A};
inline constexpr uint8_t NODE_NETWORK_TEST_MAC_B[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0B};

/** Delivers packets both ways until the outboxes are quiet. */
inline void pumpNodeNetworkPackets(NodeNetworkDevice& first, NodeNetworkDevice& second) {
    for (int round = 0; round < 20; round++) {
        const bool firstDelivered = first.deliverTo(second);
        const bool secondDelivered = second.deliverTo(first);
        if (!firstDelivered && !secondDelivered) {
            return;
        }
    }
}

/** A single device whose peer (DUEL_TEST_PEER_MAC) is scripted by the test. */
class MatchManagerV2TestSuite : public testing::Test {
public:
    void SetUp() override {
        device = new NodeNetworkDevice(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM,
                                     NODE_NETWORK_TEST_MAC_A, DUEL_TEST_PEER_MAC,
                                     NODE_NETWORK_TEST_INITIATOR_MS);
        manager = device->manager;
    }

    void TearDown() override {
        delete device;
        device = nullptr;
        manager = nullptr;
    }

    /** Starts as initiator and completes the handshake with a scripted ACCEPT. */
    void completeHandshakeAsInitiator() {
        manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
        ASSERT_TRUE(manager->startMatch(DUEL_TEST_PEER_MAC));
        NodeNetworkPacket accept = makeNodeNetworkPacket(NodeNetworkCommand::ACCEPT, false);
        nodeNetworkPacketSetUuid(accept.matchId, manager->getSession().getMatchId());
        manager->onPacket(DUEL_TEST_PEER_MAC, accept);
        ASSERT_TRUE(manager->isHandshakeComplete());
    }

    /** Primes as responder through a scripted OFFER. */
    void completeHandshakeAsResponder() {
        manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
        NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
        offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;
        manager->onPacket(DUEL_TEST_PEER_MAC, offer);
        ASSERT_TRUE(manager->isHandshakeComplete());
    }

    /** A packet from the scripted peer for the active match (peer is the opposite side). */
    NodeNetworkPacket makePeerPacket(NodeNetworkCommand command) {
        const bool peerIsInitiator = manager->getSession().getLocalSide() == DuelLinkSide::RESPONDER;
        NodeNetworkPacket packet = makeNodeNetworkPacket(command, peerIsInitiator);
        nodeNetworkPacketSetUuid(packet.matchId, manager->getSession().getMatchId());
        return packet;
    }

    /** Peer uptime minus local uptime in scripted SYNC_REPLYs. */
    static constexpr uint32_t PEER_UPTIME_OFFSET_MS = 50000;

    /**
     * Initiator: handshakes, sends one probe, and answers it with a reply whose round trip is
     * @p roundTripMs (even) and whose offset is PEER_UPTIME_OFFSET_MS. Clears the outbox.
     */
    void syncAsInitiator(uint32_t roundTripMs = 10) {
        completeHandshakeAsInitiator();
        manager->sendSyncProbe();
        ASSERT_FALSE(device->wireless.sent.empty());
        const NodeNetworkPacket probe = device->wireless.sent.back().packet;
        device->clock.advance(roundTripMs);
        NodeNetworkPacket reply = makePeerPacket(NodeNetworkCommand::SYNC_REPLY);
        reply.syncOriginMs = probe.syncOriginMs;
        reply.syncReceiveMs = probe.syncOriginMs + roundTripMs / 2 + PEER_UPTIME_OFFSET_MS;
        reply.syncTransmitMs = reply.syncReceiveMs;
        manager->onPacket(DUEL_TEST_PEER_MAC, reply);
        device->wireless.sent.clear();
    }

    /** Responder: delivers SEQUENCE with step 0 @p leadMs from now on the local clock. */
    void deliverSequence(uint8_t buttonMask, uint8_t initiatorPresserMask,
                         int32_t leadMs = static_cast<int32_t>(NODE_COUNTDOWN_MS)) {
        NodeNetworkPacket packet = makePeerPacket(NodeNetworkCommand::SEQUENCE);
        encodeSingleButtonPrompts(packet.stepPrompts, buttonMask, initiatorPresserMask);
        packet.stepZeroAtMs =
        static_cast<uint32_t>(device->clock.milliseconds()) + static_cast<uint32_t>(leadMs);
        manager->onPacket(DUEL_TEST_PEER_MAC, packet);
    }

    /** Delivers the scripted peer's INPUT_RESULT for @p step. */
    void deliverInputResult(uint8_t step, bool success, uint8_t pressCount = 1) {
        NodeNetworkPacket result = makePeerPacket(NodeNetworkCommand::INPUT_RESULT);
        result.stepIndex = step;
        result.inputSuccess = success ? 1 : 0;
        result.pressCount = pressCount;
        manager->onPacket(DUEL_TEST_PEER_MAC, result);
    }

    /** Delivers the scripted peer's INPUT_PRESS for @p step. */
    void deliverInputPress(uint8_t step, uint8_t pressCount) {
        NodeNetworkPacket press = makePeerPacket(NodeNetworkCommand::INPUT_PRESS);
        press.stepIndex = step;
        press.pressCount = pressCount;
        manager->onPacket(DUEL_TEST_PEER_MAC, press);
    }

    /** Feeds the peer's ACK for @p packet through the local wireless layer. */
    void ackFromPeer(const NodeNetworkPacket& packet) {
        NodeNetworkPacket ack;
        ack.command = static_cast<uint8_t>(NodeNetworkCommand::ACK);
        ack.sequenceNumber = packet.sequenceNumber;
        memcpy(ack.matchId, packet.matchId, sizeof(ack.matchId));
        feedNodeNetworkPacket(device->wireless, DUEL_TEST_PEER_MAC, ack);
    }

    /** @return the last captured packet carrying @p command, or a default packet. */
    NodeNetworkPacket lastSent(NodeNetworkCommand command) const {
        for (size_t i = device->wireless.sent.size(); i > 0; i--) {
            const NodeNetworkPacket& packet = device->wireless.sent[i - 1].packet;
            if (packet.command == static_cast<uint8_t>(command)) {
                return packet;
            }
        }
        return NodeNetworkPacket();
    }

    /**
     * Plays a whole round as initiator, pressing every step locally; the first @p successes
     * steps are correct. Leaves the outcome known and the session active.
     */
    void completeRoundAsInitiator(uint8_t successes) {
        syncAsInitiator();
        ASSERT_TRUE(manager->beginRound(makeSoloSequence(DuelLinkSide::INITIATOR)));
        for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
            ASSERT_TRUE(manager->onLocalInputResult(step, step < successes, 1));
        }
        ASSERT_TRUE(manager->finishRound());
    }

    /** Every step pressed by @p presser, alternating primary and secondary. */
    static NodeInputSequence makeSoloSequence(DuelLinkSide presser) {
        return makeAlternatingTestSequence(presser, presser);
    }

    NodeNetworkDevice* device = nullptr;
    MatchManagerV2* manager = nullptr;
};

// ============================================
// startMatch
// ============================================

inline void matchManagerV2ClearFailedAttemptsOnSleepPolicy(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    suite->manager->recordLinkOutcome(false);
    suite->manager->recordLinkOutcome(false);
    EXPECT_EQ(suite->manager->getFailedAttempts(), 2u);
    suite->manager->clearFailedAttemptsForCurrentPeer();
    EXPECT_EQ(suite->manager->getFailedAttempts(), 0u);
    EXPECT_EQ(suite->manager->getRetriesRemaining(), MAX_FAILED_ATTEMPTS);
}

inline void matchManagerV2RetriesRemainingAfterFailure(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(2);
    suite->manager->recordLinkOutcome(false);
    EXPECT_EQ(suite->manager->getRetriesRemaining(), MAX_FAILED_ATTEMPTS - 1);
}

inline void matchManagerV2StartMatchSendsOffer(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);

    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& sent = suite->device->wireless.sent[0];
    EXPECT_EQ(memcmp(sent.destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
    EXPECT_EQ(sent.packet.command, static_cast<uint8_t>(NodeNetworkCommand::OFFER));
    EXPECT_EQ(sent.packet.senderIsInitiator, 1);
    EXPECT_TRUE(nodeNetworkPacketUuidEquals(sent.packet.senderPlayerId, makeTestUuid(DUEL_TEST_LOCAL_ID)));
    EXPECT_TRUE(nodeNetworkPacketUuidEquals(sent.packet.senderTeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM)));
    EXPECT_EQ(sent.packet.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_FALSE(nodeNetworkPacketUuidIsZero(sent.packet.matchId));
    EXPECT_EQ(nodeNetworkPacketGetUuid(sent.packet.matchId), suite->manager->getSession().getMatchId());
    EXPECT_TRUE(suite->manager->getSession().isActive());
    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2StartMatchGeneratesDistinctMatchIds(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    const UUIDBytes first = suite->manager->getSession().getMatchId();
    suite->manager->clearCurrentMatch();

    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    EXPECT_NE(first, suite->manager->getSession().getMatchId());
}

inline void matchManagerV2StartMatchRefusedWithoutRegistration() {
    PlayerProvider emptyProvider;
    InMemoryStorage backing;
    FakeNodeNetworkWirelessManager wireless;
    FakeRemoteDeviceCoordinator rdc;
    FakePlatformClock clock;
    clock.setUnixTimeMs(NODE_NETWORK_TEST_INITIATOR_MS);
    MatchManagerV2 manager(&emptyProvider, &backing, &wireless, &rdc, &clock);
    manager.setLocalLinkSide(DuelLinkSide::INITIATOR);

    EXPECT_FALSE(manager.startMatch(DUEL_TEST_PEER_MAC));

    EXPECT_TRUE(wireless.sent.empty());
    EXPECT_FALSE(manager.getSession().isActive());
}

inline void matchManagerV2StartMatchRefusedWithNilPlayerId() {
    PlayerProvider provider;
    provider.getPlayer()->applyRegistrationProfile(UUIDBytes(), "Demo", Team(), {});
    InMemoryStorage backing;
    FakeNodeNetworkWirelessManager wireless;
    FakeRemoteDeviceCoordinator rdc;
    FakePlatformClock clock;
    clock.setUnixTimeMs(NODE_NETWORK_TEST_INITIATOR_MS);
    MatchManagerV2 manager(&provider, &backing, &wireless, &rdc, &clock);
    manager.setLocalLinkSide(DuelLinkSide::INITIATOR);

    EXPECT_FALSE(manager.startMatch(DUEL_TEST_PEER_MAC));
    EXPECT_TRUE(wireless.sent.empty());
}

inline void matchManagerV2StartMatchRefusedWhenClockNotPositive(MatchManagerV2TestSuite* suite) {
    suite->device->clock.setUnixTimeMs(0);
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);

    EXPECT_FALSE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    EXPECT_TRUE(suite->device->wireless.sent.empty());
    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void matchManagerV2StartMatchRefusedForResponder(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);

    EXPECT_FALSE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2StartMatchRefusedWhileActive(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    const UUIDBytes firstMatchId = suite->manager->getSession().getMatchId();

    EXPECT_FALSE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    EXPECT_EQ(suite->device->wireless.sent.size(), 1u);
    EXPECT_EQ(suite->manager->getSession().getMatchId(), firstMatchId);
}

// ============================================
// Handshake: initiator side
// ============================================

inline void matchManagerV2InitiatorCompletesHandshakeOnAccept(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();

    const DuelSession& session = suite->manager->getSession();
    EXPECT_TRUE(session.hasOpponentParticipant());
    EXPECT_EQ(session.getOpponentParticipant().playerId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(session.getOpponentParticipant().teamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
}

inline void matchManagerV2InitiatorIgnoresAcceptFromOtherMac(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket accept = makeNodeNetworkPacket(NodeNetworkCommand::ACCEPT, false);
    nodeNetworkPacketSetUuid(accept.matchId, suite->manager->getSession().getMatchId());

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, accept);

    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2InitiatorIgnoresAcceptForOtherMatch(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    const NodeNetworkPacket accept = makeNodeNetworkPacket(NodeNetworkCommand::ACCEPT, false);

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, accept);

    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2InitiatorIgnoresAcceptClaimingInitiator(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket accept = makeNodeNetworkPacket(NodeNetworkCommand::ACCEPT, true);
    nodeNetworkPacketSetUuid(accept.matchId, suite->manager->getSession().getMatchId());

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, accept);

    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2InitiatorIgnoresAcceptWithNilPlayerId(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket accept = makeNodeNetworkPacket(NodeNetworkCommand::ACCEPT, false);
    nodeNetworkPacketSetUuid(accept.matchId, suite->manager->getSession().getMatchId());
    memset(accept.senderPlayerId, 0, UUIDBytes::SIZE);

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, accept);

    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2RoleMismatchFromOpponentClearsSession(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket mismatch = makeNodeNetworkPacket(NodeNetworkCommand::ROLE_MISMATCH, false);
    nodeNetworkPacketSetUuid(mismatch.matchId, suite->manager->getSession().getMatchId());

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, mismatch);

    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void matchManagerV2RoleMismatchFromStrangerIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket mismatch = makeNodeNetworkPacket(NodeNetworkCommand::ROLE_MISMATCH, false);
    nodeNetworkPacketSetUuid(mismatch.matchId, suite->manager->getSession().getMatchId());

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, mismatch);

    EXPECT_TRUE(suite->manager->getSession().isActive());
}

// ============================================
// Handshake: responder side
// ============================================

inline void matchManagerV2ResponderPrimesAndAccepts(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    const DuelSession& session = suite->manager->getSession();
    EXPECT_TRUE(session.isActive());
    EXPECT_EQ(session.getLocalSide(), DuelLinkSide::RESPONDER);
    EXPECT_EQ(session.getMatchId(), nodeNetworkPacketGetUuid(offer.matchId));
    EXPECT_EQ(session.getOccurredAtUnixMs(), NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_EQ(session.getOpponentParticipant().playerId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(session.getOpponentParticipant().teamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
    EXPECT_TRUE(suite->manager->isHandshakeComplete());

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& reply = suite->device->wireless.sent[0];
    EXPECT_EQ(memcmp(reply.destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
    EXPECT_EQ(reply.packet.command, static_cast<uint8_t>(NodeNetworkCommand::ACCEPT));
    EXPECT_EQ(reply.packet.senderIsInitiator, 0);
    EXPECT_EQ(nodeNetworkPacketGetUuid(reply.packet.matchId), nodeNetworkPacketGetUuid(offer.matchId));
    EXPECT_TRUE(nodeNetworkPacketUuidEquals(reply.packet.senderPlayerId, makeTestUuid(DUEL_TEST_LOCAL_ID)));
    EXPECT_TRUE(nodeNetworkPacketUuidEquals(reply.packet.senderTeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM)));
}

inline void matchManagerV2ResponderIgnoresOfferFromStranger(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, offer);

    EXPECT_FALSE(suite->manager->getSession().isActive());
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2ResponderIgnoresOfferWithoutRegistration() {
    PlayerProvider emptyProvider;
    InMemoryStorage backing;
    FakeNodeNetworkWirelessManager wireless;
    FakeRemoteDeviceCoordinator rdc;
    rdc.setPeerMac(SerialIdentifier::INPUT_JACK, DUEL_TEST_PEER_MAC);
    FakePlatformClock clock;
    clock.setUnixTimeMs(NODE_NETWORK_TEST_RESPONDER_MS);
    MatchManagerV2 manager(&emptyProvider, &backing, &wireless, &rdc, &clock);
    manager.setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    manager.onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_FALSE(manager.getSession().isActive());
    EXPECT_TRUE(wireless.sent.empty());
}

inline void matchManagerV2ResponderIgnoresOfferWithNilPlayerId(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;
    memset(offer.senderPlayerId, 0, UUIDBytes::SIZE);

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void matchManagerV2ResponderIgnoresOfferClaimingResponder(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, false);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void matchManagerV2ResponderIgnoresOfferWhileActive(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    const UUIDBytes activeMatchId = suite->manager->getSession().getMatchId();
    suite->device->wireless.sent.clear();
    NodeNetworkPacket second = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    nodeNetworkPacketSetUuid(second.matchId, makeTestUuid(DUEL_TEST_PEER_ID));
    second.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, second);

    EXPECT_EQ(suite->manager->getSession().getMatchId(), activeMatchId);
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2OfferWhileInitiatorRepliesRoleMismatch(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_FALSE(suite->manager->getSession().isActive());
    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    EXPECT_EQ(suite->device->wireless.sent[0].packet.command,
              static_cast<uint8_t>(NodeNetworkCommand::ROLE_MISMATCH));
    EXPECT_EQ(nodeNetworkPacketGetUuid(suite->device->wireless.sent[0].packet.matchId),
              nodeNetworkPacketGetUuid(offer.matchId));
    EXPECT_EQ(memcmp(suite->device->wireless.sent[0].destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
}

// ============================================
// Timestamp agreement and correction
// ============================================

inline void matchManagerV2ResponderRejectsNonPositiveOfferTimestamp(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = 0;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_FALSE(suite->manager->getSession().isActive());
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2ResponderAcceptsOrdinarySkew(MatchManagerV2TestSuite* suite) {
    suite->device->clock.setUnixTimeMs(NODE_NETWORK_TEST_RESPONDER_MS);
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_TRUE(suite->manager->isHandshakeComplete());
    EXPECT_EQ(suite->manager->getSession().getOccurredAtUnixMs(), NODE_NETWORK_TEST_INITIATOR_MS);
}

inline void matchManagerV2ResponderAcceptsOfferAheadOfItsClock(MatchManagerV2TestSuite* suite) {
    suite->device->clock.setUnixTimeMs(NODE_NETWORK_TEST_INITIATOR_MS);
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = NODE_NETWORK_TEST_RESPONDER_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_TRUE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2ResponderCorrectsImplausibleOffer(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = 90000;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_FALSE(suite->manager->getSession().isActive());
    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& reply = suite->device->wireless.sent[0];
    EXPECT_EQ(reply.packet.command, static_cast<uint8_t>(NodeNetworkCommand::TIME_CORRECTION));
    EXPECT_EQ(reply.packet.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_EQ(nodeNetworkPacketGetUuid(reply.packet.matchId), nodeNetworkPacketGetUuid(offer.matchId));
    EXPECT_EQ(memcmp(reply.destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
}

inline void matchManagerV2ResponderWithoutClockRejectsOffer(MatchManagerV2TestSuite* suite) {
    suite->device->clock.setUnixTimeMs(0);
    suite->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    NodeNetworkPacket offer = makeNodeNetworkPacket(NodeNetworkCommand::OFFER, true);
    offer.occurredAtUnixMs = 90000;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, offer);

    EXPECT_FALSE(suite->manager->getSession().isActive());
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2InitiatorResendsOfferOnceAfterCorrection(MatchManagerV2TestSuite* suite) {
    suite->device->clock.setUnixTimeMs(90000);
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    const UUIDBytes matchId = suite->manager->getSession().getMatchId();
    suite->device->wireless.sent.clear();
    NodeNetworkPacket correction = makeNodeNetworkPacket(NodeNetworkCommand::TIME_CORRECTION, false);
    nodeNetworkPacketSetUuid(correction.matchId, matchId);
    correction.occurredAtUnixMs = NODE_NETWORK_TEST_RESPONDER_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, correction);

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& resent = suite->device->wireless.sent[0];
    EXPECT_EQ(resent.packet.command, static_cast<uint8_t>(NodeNetworkCommand::OFFER));
    EXPECT_EQ(nodeNetworkPacketGetUuid(resent.packet.matchId), matchId);
    EXPECT_EQ(resent.packet.occurredAtUnixMs, NODE_NETWORK_TEST_RESPONDER_MS);
    EXPECT_EQ(suite->manager->getSession().getOccurredAtUnixMs(), NODE_NETWORK_TEST_RESPONDER_MS);

    suite->device->wireless.sent.clear();
    suite->manager->onPacket(DUEL_TEST_PEER_MAC, correction);
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2InitiatorIgnoresCorrectionFromStranger(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    suite->device->wireless.sent.clear();
    NodeNetworkPacket correction = makeNodeNetworkPacket(NodeNetworkCommand::TIME_CORRECTION, false);
    nodeNetworkPacketSetUuid(correction.matchId, suite->manager->getSession().getMatchId());
    correction.occurredAtUnixMs = NODE_NETWORK_TEST_RESPONDER_MS;

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, correction);

    EXPECT_TRUE(suite->device->wireless.sent.empty());
    EXPECT_EQ(suite->manager->getSession().getOccurredAtUnixMs(), NODE_NETWORK_TEST_INITIATOR_MS);
}

inline void matchManagerV2InitiatorIgnoresNonPositiveCorrection(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    suite->device->wireless.sent.clear();
    NodeNetworkPacket correction = makeNodeNetworkPacket(NodeNetworkCommand::TIME_CORRECTION, false);
    nodeNetworkPacketSetUuid(correction.matchId, suite->manager->getSession().getMatchId());
    correction.occurredAtUnixMs = 0;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, correction);

    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

// ============================================
// Round: initiator side
// ============================================

inline void matchManagerV2SyncProbeCarriesUptime(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    suite->device->wireless.sent.clear();
    suite->device->clock.setTime(1234);

    suite->manager->sendSyncProbe();

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& probe = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(probe.command, static_cast<uint8_t>(NodeNetworkCommand::SYNC));
    EXPECT_EQ(probe.syncOriginMs, 1234u);
    EXPECT_EQ(probe.reliable, 0);
    EXPECT_EQ(memcmp(suite->device->wireless.sent[0].destinationMac.data(), DUEL_TEST_PEER_MAC, 6),
              0);
}

inline void matchManagerV2SyncReplyGivesOffsetAndRoundTrip(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator(20);

    const DuelSession& session = suite->manager->getSession();
    ASSERT_TRUE(session.hasSyncSample());
    EXPECT_EQ(session.getSyncOffsetMs(),
              static_cast<int32_t>(MatchManagerV2TestSuite::PEER_UPTIME_OFFSET_MS));
    EXPECT_EQ(session.getSyncRoundTripMs(), 20u);
    EXPECT_TRUE(suite->manager->hasClockSync());
}

inline void matchManagerV2SlowSyncIsNotGoodEnough(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator(NODE_SYNC_MAX_RTT_MS + 2);

    EXPECT_TRUE(suite->manager->getSession().hasSyncSample());
    EXPECT_FALSE(suite->manager->hasClockSync());
    EXPECT_FALSE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));
}

inline void matchManagerV2SyncReplyFromStrangerIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    NodeNetworkPacket reply = suite->makePeerPacket(NodeNetworkCommand::SYNC_REPLY);

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, reply);

    EXPECT_FALSE(suite->manager->getSession().hasSyncSample());
}

inline void matchManagerV2BeginRoundRefusedBeforeHandshake(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    EXPECT_FALSE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));
}

inline void matchManagerV2BeginRoundRefusedWithoutSync(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    suite->device->wireless.sent.clear();

    EXPECT_FALSE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2BeginRoundRefusedForResponder(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();

    EXPECT_FALSE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));
}

inline void matchManagerV2BeginRoundSendsReliableSequence(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    const uint32_t now = static_cast<uint32_t>(suite->device->clock.milliseconds());

    ASSERT_TRUE(
    suite->manager->beginRound(makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER)));

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& sequence = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(sequence.command, static_cast<uint8_t>(NodeNetworkCommand::SEQUENCE));
    EXPECT_EQ(sequence.reliable, 1);
    EXPECT_EQ(sequence.senderIsInitiator, 1);
    const uint8_t expectedPrompts[NODE_NETWORK_PACKET_STEPS] = {0x01, 0x50, 0x01, 0x50, 0x01};
    EXPECT_EQ(memcmp(sequence.stepPrompts, expectedPrompts, sizeof(expectedPrompts)), 0);
    EXPECT_EQ(sequence.stepZeroAtMs,
              now + NODE_COUNTDOWN_MS + MatchManagerV2TestSuite::PEER_UPTIME_OFFSET_MS);
    EXPECT_EQ(suite->manager->msUntilStep(0), static_cast<int32_t>(NODE_COUNTDOWN_MS));
    EXPECT_EQ(suite->manager->msUntilStep(2),
              static_cast<int32_t>(NODE_COUNTDOWN_MS +
                                   2 * (NODE_INPUT_WINDOW_MS + NODE_STEP_GAP_MS)));
    EXPECT_EQ(suite->manager->msUntilWindowCloses(0),
              static_cast<int32_t>(NODE_COUNTDOWN_MS + NODE_INPUT_WINDOW_MS));
    EXPECT_FALSE(
    suite->manager->beginRound(makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER)));
}

inline void matchManagerV2SequenceAckMarksDelivered(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));

    suite->ackFromPeer(suite->lastSent(NodeNetworkCommand::SEQUENCE));

    EXPECT_TRUE(suite->manager->getSession().isSequenceDelivered());
    EXPECT_EQ(suite->device->wireless.getPendingReliableCount(), 0u);
}

inline void matchManagerV2UndeliveredSequenceIsFlagged(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));

    for (int tick = 0; tick < NODE_RELIABLE_MAX_ATTEMPTS; tick++) {
        suite->device->clock.advance(NODE_RELIABLE_RETRY_MS);
        suite->device->wireless.update();
    }

    EXPECT_EQ(suite->device->wireless.countSent(NodeNetworkCommand::SEQUENCE),
              static_cast<size_t>(NODE_RELIABLE_MAX_ATTEMPTS));
    EXPECT_TRUE(suite->manager->getSession().isSequenceUndelivered());
    EXPECT_FALSE(suite->manager->getSession().isSequenceDelivered());
}

inline void matchManagerV2InitiatorRecordsResponderResult(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::RESPONDER)));

    suite->deliverInputResult(0, true);

    EXPECT_EQ(suite->manager->getSession().getStepResult(0), NodeStepResult::SUCCESS);
}

inline void matchManagerV2InitiatorIgnoresResultForItsOwnStep(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));

    suite->deliverInputResult(0, true);

    EXPECT_FALSE(suite->manager->getSession().hasStepResult(0));
}

inline void matchManagerV2InitiatorIgnoresResultFromStranger(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::RESPONDER)));
    NodeNetworkPacket result = suite->makePeerPacket(NodeNetworkCommand::INPUT_RESULT);
    result.inputSuccess = 1;

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, result);

    EXPECT_FALSE(suite->manager->getSession().hasStepResult(0));
}

inline void matchManagerV2LateResultAfterExpiryStaysFailed(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::RESPONDER)));

    suite->manager->expireStep(0);
    suite->deliverInputResult(0, true);

    EXPECT_EQ(suite->manager->getSession().getStepResult(0), NodeStepResult::FAILED);
}

inline void matchManagerV2LocalResultRefusedForOpponentStep(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::RESPONDER)));

    EXPECT_FALSE(suite->manager->onLocalInputResult(0, true, 1));
    EXPECT_FALSE(suite->manager->getSession().hasStepResult(0));
}

inline void matchManagerV2InitiatorLocalResultIsSentReliably(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));
    suite->device->wireless.sent.clear();

    EXPECT_TRUE(suite->manager->onLocalInputResult(0, true, 1));
    EXPECT_FALSE(suite->manager->onLocalInputResult(0, false, 1));

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& result = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(result.command, static_cast<uint8_t>(NodeNetworkCommand::INPUT_RESULT));
    EXPECT_EQ(result.reliable, 1);
    EXPECT_EQ(result.senderIsInitiator, 1);
    EXPECT_EQ(result.stepIndex, 0);
    EXPECT_EQ(result.inputSuccess, 1);
    EXPECT_EQ(result.pressCount, 1);
    EXPECT_EQ(suite->manager->getSession().getStepResult(0), NodeStepResult::SUCCESS);
}

inline void matchManagerV2LocalPressIsSentUnreliably(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR)));
    suite->device->wireless.sent.clear();

    suite->manager->onLocalPress(0, 1);

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& press = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(press.command, static_cast<uint8_t>(NodeNetworkCommand::INPUT_PRESS));
    EXPECT_EQ(press.reliable, 0);
    EXPECT_EQ(press.stepIndex, 0);
    EXPECT_EQ(press.pressCount, 1);
}

inline void matchManagerV2OpponentPressesAreRecorded(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    ASSERT_TRUE(
    suite->manager->beginRound(MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::RESPONDER)));

    suite->deliverInputPress(0, 1);
    EXPECT_EQ(suite->manager->getSession().getOpponentPresses(0), 1);
    EXPECT_FALSE(suite->manager->getSession().hasStepResult(0));

    suite->deliverInputResult(1, true, 2);
    EXPECT_EQ(suite->manager->getSession().getOpponentPresses(1), 2);
}

inline void matchManagerV2BothPressStepSucceedsOnlyWhenBothSucceed(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    NodeInputSequence sequence = MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR);
    const NodePrompt prompt = makeNodePrompt(ButtonIdentifier::PRIMARY_BUTTON);
    sequence.inputs[0] = makeBothNodeInput(prompt, prompt);
    sequence.inputs[1] = makeBothNodeInput(prompt, prompt);
    ASSERT_TRUE(suite->manager->beginRound(sequence));

    ASSERT_TRUE(suite->manager->onLocalInputResult(0, true, 1));
    EXPECT_FALSE(suite->manager->getSession().hasStepResult(0));
    suite->deliverInputResult(0, true);
    EXPECT_EQ(suite->manager->getSession().getStepResult(0), NodeStepResult::SUCCESS);

    ASSERT_TRUE(suite->manager->onLocalInputResult(1, true, 1));
    suite->deliverInputResult(1, false);
    EXPECT_EQ(suite->manager->getSession().getStepResult(1), NodeStepResult::FAILED);

    EXPECT_EQ(suite->manager->getSession().countSuccesses(DuelLinkSide::INITIATOR), 1);
    EXPECT_EQ(suite->manager->getSession().countSuccesses(DuelLinkSide::RESPONDER), 0);
}

inline void matchManagerV2SequenceEncodesTwoButtonAndBothPrompts(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    NodeInputSequence sequence = MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR);
    sequence.inputs[0] = NodeInput();
    sequence.inputs[0].responder =
    makeNodePrompt(ButtonIdentifier::SECONDARY_BUTTON, ButtonIdentifier::PRIMARY_BUTTON);
    sequence.inputs[1] = makeBothNodeInput(
    makeNodePrompt(ButtonIdentifier::PRIMARY_BUTTON, ButtonIdentifier::SECONDARY_BUTTON),
    makeNodePrompt(ButtonIdentifier::SECONDARY_BUTTON));
    ASSERT_TRUE(suite->manager->beginRound(sequence));

    const NodeNetworkPacket packet = suite->lastSent(NodeNetworkCommand::SEQUENCE);

    EXPECT_EQ(packet.stepPrompts[0], 0x60);
    EXPECT_EQ(packet.stepPrompts[1], 0x5A);
}

inline void
matchManagerV2FinishRoundFailsUnresolvedStepsAndSendsOutcome(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();
    NodeInputSequence sequence = MatchManagerV2TestSuite::makeSoloSequence(DuelLinkSide::INITIATOR);
    sequence.inputs[1] = makeNodeInput(ButtonIdentifier::PRIMARY_BUTTON, DuelLinkSide::RESPONDER);
    sequence.inputs[2] = makeNodeInput(ButtonIdentifier::PRIMARY_BUTTON, DuelLinkSide::RESPONDER);
    ASSERT_TRUE(suite->manager->beginRound(sequence));
    suite->manager->onLocalInputResult(0, true, 1);
    suite->deliverInputResult(1, true);
    suite->manager->onLocalInputResult(3, true, 1);
    suite->device->wireless.sent.clear();

    ASSERT_TRUE(suite->manager->finishRound());

    const DuelSession& session = suite->manager->getSession();
    EXPECT_EQ(session.getStepResult(2), NodeStepResult::FAILED);
    EXPECT_EQ(session.getStepResult(4), NodeStepResult::FAILED);
    EXPECT_TRUE(session.isOutcomeKnown());
    EXPECT_EQ(session.getInitiatorScore(), 2);
    EXPECT_EQ(session.getResponderScore(), 1);
    EXPECT_FALSE(session.isRoundSuccess());
    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& outcome = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(outcome.command, static_cast<uint8_t>(NodeNetworkCommand::OUTCOME));
    EXPECT_EQ(outcome.reliable, 1);
    EXPECT_EQ(outcome.initiatorScore, 2);
    EXPECT_EQ(outcome.responderScore, 1);
    EXPECT_FALSE(suite->manager->finishRound());
}

inline void matchManagerV2OutcomeKeepsResendingAfterConcluding(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(5);
    suite->manager->concludeRound();
    ASSERT_FALSE(suite->manager->getSession().isActive());
    suite->device->wireless.sent.clear();

    suite->device->clock.advance(NODE_RELIABLE_RETRY_MS);
    suite->device->wireless.update();

    const NodeNetworkPacket outcome = suite->lastSent(NodeNetworkCommand::OUTCOME);
    EXPECT_EQ(outcome.command, static_cast<uint8_t>(NodeNetworkCommand::OUTCOME));
    EXPECT_EQ(outcome.initiatorScore, 5);
}

inline void matchManagerV2OutcomeAckMarksDelivered(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(5);
    ASSERT_FALSE(suite->manager->getSession().isOutcomeDelivered());

    suite->ackFromPeer(suite->lastSent(NodeNetworkCommand::OUTCOME));

    EXPECT_TRUE(suite->manager->getSession().isOutcomeDelivered());
    EXPECT_FALSE(suite->manager->getSession().isOutcomeUndelivered());
}

inline void matchManagerV2UndeliveredOutcomeIsFlagged(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(5);

    for (int tick = 0; tick < NODE_RELIABLE_MAX_ATTEMPTS; tick++) {
        suite->device->clock.advance(NODE_RELIABLE_RETRY_MS);
        suite->device->wireless.update();
    }

    EXPECT_TRUE(suite->manager->getSession().isOutcomeUndelivered());
    EXPECT_FALSE(suite->manager->getSession().isOutcomeDelivered());
}

// ============================================
// Round: responder side
// ============================================

inline void matchManagerV2ResponderAnswersSync(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->device->wireless.sent.clear();
    suite->device->clock.setTime(5000);
    NodeNetworkPacket probe = suite->makePeerPacket(NodeNetworkCommand::SYNC);
    probe.syncOriginMs = 77;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, probe);

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& reply = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(reply.command, static_cast<uint8_t>(NodeNetworkCommand::SYNC_REPLY));
    EXPECT_EQ(reply.reliable, 0);
    EXPECT_EQ(reply.senderIsInitiator, 0);
    EXPECT_EQ(reply.syncOriginMs, 77u);
    EXPECT_EQ(reply.syncReceiveMs, 5000u);
    EXPECT_EQ(reply.syncTransmitMs, 5000u);
}

inline void matchManagerV2ResponderIgnoresSyncFromStranger(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->device->wireless.sent.clear();

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, suite->makePeerPacket(NodeNetworkCommand::SYNC));

    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2ResponderStoresSequence(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();

    suite->deliverSequence(0x0A, 0x15);

    const DuelSession& session = suite->manager->getSession();
    ASSERT_TRUE(session.hasSequence());
    EXPECT_EQ(session.getPrompt(0, DuelLinkSide::INITIATOR).count, 1);
    EXPECT_EQ(session.getPrompt(0, DuelLinkSide::INITIATOR).buttons[0],
              ButtonIdentifier::PRIMARY_BUTTON);
    EXPECT_EQ(session.getPrompt(0, DuelLinkSide::RESPONDER).count, 0);
    EXPECT_EQ(session.getPrompt(1, DuelLinkSide::RESPONDER).buttons[0],
              ButtonIdentifier::SECONDARY_BUTTON);
    EXPECT_FALSE(session.isLocalPresser(0));
    EXPECT_TRUE(session.isLocalPresser(1));
    EXPECT_EQ(suite->manager->msUntilStep(0), static_cast<int32_t>(NODE_COUNTDOWN_MS));
    EXPECT_FALSE(suite->manager->isAborted());
}

inline void matchManagerV2ResponderAbortsOnPastSequence(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->device->wireless.sent.clear();

    suite->deliverSequence(0, 0, 0);

    EXPECT_TRUE(suite->manager->isAborted());
    EXPECT_FALSE(suite->manager->getSession().isActive());
    const NodeNetworkPacket abort = suite->lastSent(NodeNetworkCommand::ABORT);
    EXPECT_EQ(abort.command, static_cast<uint8_t>(NodeNetworkCommand::ABORT));
    EXPECT_EQ(abort.reliable, 1);
    EXPECT_EQ(suite->manager->getFailedAttempts(), 0u);
}

inline void matchManagerV2ResponderAbortsOnFarFutureSequence(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();

    suite->deliverSequence(
    0, 0, static_cast<int32_t>(NODE_COUNTDOWN_MS) + NODE_SEQUENCE_LEAD_SLACK_MS + 1);

    EXPECT_TRUE(suite->manager->isAborted());
    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void matchManagerV2ResponderLocalResultIsSentReliably(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->deliverSequence(0x01, 0x00);
    suite->device->wireless.sent.clear();

    EXPECT_TRUE(suite->manager->onLocalInputResult(0, true, 1));

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& result = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(result.command, static_cast<uint8_t>(NodeNetworkCommand::INPUT_RESULT));
    EXPECT_EQ(result.reliable, 1);
    EXPECT_EQ(result.senderIsInitiator, 0);
    EXPECT_EQ(result.stepIndex, 0);
    EXPECT_EQ(result.inputSuccess, 1);
}

inline void matchManagerV2ResponderViewerCannotReportResult(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->deliverSequence(0x00, 0x01);
    suite->device->wireless.sent.clear();

    EXPECT_FALSE(suite->manager->getSession().isLocalPresser(0));
    EXPECT_FALSE(suite->manager->onLocalInputResult(0, true, 1));
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2ResponderAbortsOnInvalidPrompts(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    NodeNetworkPacket packet = suite->makePeerPacket(NodeNetworkCommand::SEQUENCE);
    encodeSingleButtonPrompts(packet.stepPrompts, 0x00, 0x1F);
    packet.stepPrompts[2] = 0x00;
    packet.stepZeroAtMs = static_cast<uint32_t>(suite->device->clock.milliseconds()) +
                          static_cast<uint32_t>(NODE_COUNTDOWN_MS);

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, packet);

    EXPECT_TRUE(suite->manager->isAborted());
    EXPECT_FALSE(suite->manager->getSession().hasSequence());
}

inline void matchManagerV2ResponderRecordsInitiatorResult(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->deliverSequence(0x00, 0x01);

    suite->deliverInputResult(0, true);

    EXPECT_EQ(suite->manager->getSession().getStepResult(0), NodeStepResult::SUCCESS);
}

inline void matchManagerV2ResponderStoresOutcome(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    NodeNetworkPacket outcome = suite->makePeerPacket(NodeNetworkCommand::OUTCOME);
    outcome.initiatorScore = 2;
    outcome.responderScore = 2;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, outcome);

    EXPECT_TRUE(suite->manager->isOutcomeKnown());
    EXPECT_TRUE(suite->manager->getSession().isRoundSuccess());
}

inline void matchManagerV2ResponderIgnoresImpossibleOutcome(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    NodeNetworkPacket outcome = suite->makePeerPacket(NodeNetworkCommand::OUTCOME);
    outcome.initiatorScore = 4;
    outcome.responderScore = 2;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, outcome);

    EXPECT_FALSE(suite->manager->isOutcomeKnown());
}

// ============================================
// Abort
// ============================================

inline void matchManagerV2AbortSendsAbortAndIsNotCounted(MatchManagerV2TestSuite* suite) {
    suite->syncAsInitiator();

    suite->manager->abortRound();

    EXPECT_TRUE(suite->manager->isAborted());
    EXPECT_FALSE(suite->manager->getSession().isActive());
    EXPECT_EQ(suite->manager->getFailedAttempts(), 0u);
    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const NodeNetworkPacket& abort = suite->device->wireless.sent[0].packet;
    EXPECT_EQ(abort.command, static_cast<uint8_t>(NodeNetworkCommand::ABORT));
    EXPECT_EQ(abort.reliable, 1);

    suite->manager->clearAbort();
    EXPECT_FALSE(suite->manager->isAborted());
}

inline void matchManagerV2AbortFromOpponentClearsSession(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, suite->makePeerPacket(NodeNetworkCommand::ABORT));

    EXPECT_TRUE(suite->manager->isAborted());
    EXPECT_FALSE(suite->manager->getSession().isActive());
    EXPECT_EQ(suite->manager->getFailedAttempts(), 0u);
}

inline void matchManagerV2AbortFromStrangerIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, suite->makePeerPacket(NodeNetworkCommand::ABORT));

    EXPECT_FALSE(suite->manager->isAborted());
    EXPECT_TRUE(suite->manager->getSession().isActive());
}

inline void matchManagerV2StartMatchClearsAbort(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    suite->manager->abortRound();
    ASSERT_TRUE(suite->manager->isAborted());

    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    EXPECT_FALSE(suite->manager->isAborted());
}

// ============================================
// Finalize and conclude
// ============================================

inline void matchManagerV2FinalizeStoresRecordAndClearsSession(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(4);

    ASSERT_TRUE(suite->manager->finalizeMatch());

    EXPECT_EQ(suite->manager->getPendingRecordCount(), 1u);
    EXPECT_FALSE(suite->manager->getSession().isActive());
    QuickdrawMatchRecord record;
    ASSERT_TRUE(suite->manager->readPendingRecord(0, record));
    EXPECT_EQ(record.playerAId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(record.playerBId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(record.playerATeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
    EXPECT_EQ(record.playerBTeamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
    EXPECT_EQ(record.winnerPlayerId, record.playerAId);
    EXPECT_EQ(record.playerADrawTimeMs, 4u);
    EXPECT_EQ(record.playerBDrawTimeMs, 0u);
    EXPECT_EQ(record.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
}

inline void matchManagerV2FinalizeRefusedBelowThreshold(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(3);

    EXPECT_FALSE(suite->manager->finalizeMatch());

    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
    EXPECT_TRUE(suite->manager->getSession().isActive());
}

inline void matchManagerV2FinalizeRefusedWithoutSession(MatchManagerV2TestSuite* suite) {
    EXPECT_FALSE(suite->manager->finalizeMatch());
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
}

inline void matchManagerV2FinalizeRefusedWhenStorageWriteFails(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(5);
    suite->device->backingStorage.failWrites = true;

    EXPECT_FALSE(suite->manager->finalizeMatch());

    suite->device->backingStorage.failWrites = false;
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
    EXPECT_TRUE(suite->manager->getSession().isActive());
}

inline void matchManagerV2ConcludeSuccessStoresAndRemembersScore(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    suite->manager->recordLinkOutcome(false);
    suite->manager->clearCurrentMatch();
    suite->completeRoundAsInitiator(4);

    suite->manager->concludeRound();

    EXPECT_EQ(suite->manager->getLastRoundScore(), 4);
    EXPECT_TRUE(suite->manager->wasLastRoundSuccess());
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 1u);
    EXPECT_EQ(suite->manager->getFailedAttempts(), 0u);
    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void matchManagerV2ConcludeFailureCountsAttempt(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(3);

    suite->manager->concludeRound();

    EXPECT_EQ(suite->manager->getLastRoundScore(), 3);
    EXPECT_FALSE(suite->manager->wasLastRoundSuccess());
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
    EXPECT_EQ(suite->manager->getFailedAttempts(), 1u);
    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void
matchManagerV2ConcludeWithoutOutcomeUsesLocalScoreAndFails(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->deliverSequence(0x00, 0x00);
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        ASSERT_TRUE(suite->manager->onLocalInputResult(step, true, 1));
    }

    suite->manager->concludeRound();

    EXPECT_EQ(suite->manager->getLastRoundScore(), static_cast<uint8_t>(NODE_SEQUENCE_LENGTH));
    EXPECT_FALSE(suite->manager->wasLastRoundSuccess());
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
    EXPECT_EQ(suite->manager->getFailedAttempts(), 1u);
}

inline void matchManagerV2ClearCurrentMatchLeavesStorageAlone(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(5);
    ASSERT_TRUE(suite->manager->finalizeMatch());
    suite->completeRoundAsInitiator(5);

    suite->manager->clearCurrentMatch();

    EXPECT_FALSE(suite->manager->getSession().isActive());
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 1u);
}

inline void matchManagerV2ClearPendingRecordsEmptiesStorage(MatchManagerV2TestSuite* suite) {
    suite->completeRoundAsInitiator(5);
    ASSERT_TRUE(suite->manager->finalizeMatch());

    suite->manager->clearPendingRecords();

    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
}

// ============================================
// Failed-attempt limit
// ============================================

inline void matchManagerV2StartsWithNoFailedAttempts(MatchManagerV2TestSuite* suite) {
    EXPECT_EQ(suite->manager->getFailedAttempts(), 0u);
    EXPECT_FALSE(suite->manager->hasExhaustedAttempts());
}

inline void matchManagerV2ExhaustsAttemptsOnThirdFailure(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    for (uint32_t failure = 1; failure <= MAX_FAILED_ATTEMPTS; failure++) {
        ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
        suite->manager->recordLinkOutcome(false);
        suite->manager->clearCurrentMatch();
        EXPECT_EQ(suite->manager->getFailedAttempts(), failure);
        EXPECT_EQ(suite->manager->hasExhaustedAttempts(), failure == MAX_FAILED_ATTEMPTS);
    }
}

inline void matchManagerV2SuccessResetsFailedAttempts(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    for (int failure = 0; failure < 2; failure++) {
        ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
        suite->manager->recordLinkOutcome(false);
        suite->manager->clearCurrentMatch();
    }
    ASSERT_EQ(suite->manager->getFailedAttempts(), 2u);

    suite->completeRoundAsInitiator(5);
    ASSERT_TRUE(suite->manager->finalizeMatch());
    suite->manager->recordLinkOutcome(true);

    EXPECT_EQ(suite->manager->getFailedAttempts(), 0u);
    EXPECT_FALSE(suite->manager->hasExhaustedAttempts());
}

inline void matchManagerV2PeerChangeResetsFailedAttempts(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    for (int failure = 0; failure < 2; failure++) {
        ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
        suite->manager->recordLinkOutcome(false);
        suite->manager->clearCurrentMatch();
    }

    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_OTHER_MAC));
    suite->manager->recordLinkOutcome(false);

    EXPECT_EQ(suite->manager->getFailedAttempts(), 1u);
    EXPECT_FALSE(suite->manager->hasExhaustedAttempts());
}

inline void matchManagerV2FailedAttemptsSurviveClearCurrentMatch(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    suite->manager->recordLinkOutcome(false);

    suite->manager->clearCurrentMatch();

    EXPECT_EQ(suite->manager->getFailedAttempts(), 1u);
}

inline void matchManagerV2FailedAttemptsAreNotStoredInRecords(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    suite->manager->recordLinkOutcome(false);
    suite->manager->clearCurrentMatch();
    suite->completeRoundAsInitiator(5);
    ASSERT_TRUE(suite->manager->finalizeMatch());

    QuickdrawMatchRecord record;
    ASSERT_TRUE(suite->manager->readPendingRecord(0, record));
    QuickdrawMatchRecord expected;
    expected.matchId = record.matchId;
    expected.playerAId = makeTestUuid(DUEL_TEST_LOCAL_ID);
    expected.playerBId = makeTestUuid(DUEL_TEST_PEER_ID);
    expected.playerATeamId = makeTestUuid(DUEL_TEST_LOCAL_TEAM);
    expected.playerBTeamId = makeTestUuid(DUEL_TEST_PEER_TEAM);
    expected.winnerPlayerId = expected.playerAId;
    expected.playerADrawTimeMs = 5;
    expected.playerBDrawTimeMs = 0;
    expected.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;
    expectRecordsEqual(expected, record);
}

// ============================================
// Two-device integration
// ============================================

class NodeNetworkIntegrationSuite : public testing::Test {
public:
    static constexpr unsigned long INITIATOR_UPTIME_MS = 1000;
    static constexpr unsigned long RESPONDER_UPTIME_MS = 777777;

    void SetUp() override {
        initiator = new NodeNetworkDevice(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM,
                                        NODE_NETWORK_TEST_MAC_A, NODE_NETWORK_TEST_MAC_B,
                                        NODE_NETWORK_TEST_INITIATOR_MS);
        responder = new NodeNetworkDevice(DUEL_TEST_PEER_ID, DUEL_TEST_PEER_TEAM,
                                        NODE_NETWORK_TEST_MAC_B, NODE_NETWORK_TEST_MAC_A,
                                        NODE_NETWORK_TEST_RESPONDER_MS);
        initiator->clock.setTime(INITIATOR_UPTIME_MS);
        responder->clock.setTime(RESPONDER_UPTIME_MS);
        initiator->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
        responder->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    }

    void TearDown() override {
        delete initiator;
        delete responder;
        initiator = nullptr;
        responder = nullptr;
    }

    /** Advances both clocks, lets both wireless layers resend, and delivers everything. */
    void advance(unsigned long ms) {
        initiator->clock.advance(ms);
        responder->clock.advance(ms);
        initiator->wireless.update();
        responder->wireless.update();
        pumpNodeNetworkPackets(*initiator, *responder);
    }

    /** Runs offer/accept so both devices hold a complete handshake. */
    void handshake() {
        ASSERT_TRUE(initiator->manager->startMatch(NODE_NETWORK_TEST_MAC_B));
        pumpNodeNetworkPackets(*initiator, *responder);
    }

    /** Handshakes, runs every sync probe, and sends @p sequence. */
    void syncAndBegin(const NodeInputSequence& sequence) {
        handshake();
        for (uint8_t probe = 0; probe < NODE_SYNC_PROBE_COUNT; probe++) {
            initiator->manager->sendSyncProbe();
            pumpNodeNetworkPackets(*initiator, *responder);
        }
        ASSERT_TRUE(initiator->manager->hasClockSync());
        ASSERT_TRUE(initiator->manager->beginRound(sequence));
        pumpNodeNetworkPackets(*initiator, *responder);
    }

    /** Even steps are pressed by the initiator, odd steps by the responder. */
    static NodeInputSequence makeAlternatingSequence() {
        return makeAlternatingTestSequence(DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER);
    }

    /**
     * Plays every step with the alternating sequence; the presser of step i reports
     * @p results[i]. Stops before the initiator finishes the round.
     */
    void playSteps(const bool (&results)[NODE_SEQUENCE_LENGTH]) {
        syncAndBegin(makeAlternatingSequence());
        for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
            NodeNetworkDevice* presser = step % 2 == 0 ? initiator : responder;
            ASSERT_TRUE(presser->manager->onLocalInputResult(step, results[step], 1));
            pumpNodeNetworkPackets(*initiator, *responder);
        }
    }

    /** Plays the round and delivers the OUTCOME. */
    void playRound(const bool (&results)[NODE_SEQUENCE_LENGTH]) {
        playSteps(results);
        ASSERT_TRUE(initiator->manager->finishRound());
        pumpNodeNetworkPackets(*initiator, *responder);
    }

    NodeNetworkDevice* initiator = nullptr;
    NodeNetworkDevice* responder = nullptr;
};

inline constexpr bool NODE_NETWORK_TEST_FOUR_OF_FIVE[NODE_SEQUENCE_LENGTH] = {true, true, false,
                                                                              true, true};
inline constexpr bool NODE_NETWORK_TEST_THREE_OF_FIVE[NODE_SEQUENCE_LENGTH] = {true, false, false,
                                                                               true, true};

inline void nodeNetworkIntegrationHandshakeSharesIdentityAndTimestamp(NodeNetworkIntegrationSuite* suite) {
    suite->handshake();

    ASSERT_TRUE(suite->initiator->manager->isHandshakeComplete());
    ASSERT_TRUE(suite->responder->manager->isHandshakeComplete());
    const DuelSession& initiatorSession = suite->initiator->manager->getSession();
    const DuelSession& responderSession = suite->responder->manager->getSession();
    EXPECT_EQ(initiatorSession.getMatchId(), responderSession.getMatchId());
    EXPECT_EQ(initiatorSession.getOccurredAtUnixMs(), NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_EQ(responderSession.getOccurredAtUnixMs(), NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_EQ(initiatorSession.getOpponentParticipant().playerId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(responderSession.getOpponentParticipant().playerId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(responderSession.getOpponentParticipant().teamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
}

inline void nodeNetworkIntegrationSyncMeasuresUptimeOffset(NodeNetworkIntegrationSuite* suite) {
    suite->handshake();

    suite->initiator->manager->sendSyncProbe();
    pumpNodeNetworkPackets(*suite->initiator, *suite->responder);

    const DuelSession& session = suite->initiator->manager->getSession();
    ASSERT_TRUE(session.hasSyncSample());
    EXPECT_EQ(session.getSyncOffsetMs(),
              static_cast<int32_t>(NodeNetworkIntegrationSuite::RESPONDER_UPTIME_MS -
                                   NodeNetworkIntegrationSuite::INITIATOR_UPTIME_MS));
    EXPECT_EQ(session.getSyncRoundTripMs(), 0u);
}

inline void nodeNetworkIntegrationBothRunTheSameSchedule(NodeNetworkIntegrationSuite* suite) {
    const NodeInputSequence sequence = NodeNetworkIntegrationSuite::makeAlternatingSequence();

    suite->syncAndBegin(sequence);

    const DuelSession& initiatorSession = suite->initiator->manager->getSession();
    const DuelSession& responderSession = suite->responder->manager->getSession();
    ASSERT_TRUE(responderSession.hasSequence());
    EXPECT_TRUE(initiatorSession.isSequenceDelivered());
    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        for (DuelLinkSide side : {DuelLinkSide::INITIATOR, DuelLinkSide::RESPONDER}) {
            const NodePrompt& sent = initiatorSession.getPrompt(step, side);
            const NodePrompt& received = responderSession.getPrompt(step, side);
            EXPECT_EQ(received.count, sent.count);
            EXPECT_EQ(received.buttons[0], sent.buttons[0]);
        }
        EXPECT_NE(initiatorSession.isLocalPresser(step), responderSession.isLocalPresser(step));
        EXPECT_EQ(suite->initiator->manager->msUntilStep(step),
                  suite->responder->manager->msUntilStep(step));
    }
    EXPECT_EQ(suite->responder->manager->msUntilStep(0), static_cast<int32_t>(NODE_COUNTDOWN_MS));
}

inline void nodeNetworkIntegrationBothSidesAgreeOnSuccess(NodeNetworkIntegrationSuite* suite) {
    suite->playRound(NODE_NETWORK_TEST_FOUR_OF_FIVE);

    const DuelSession& initiatorSession = suite->initiator->manager->getSession();
    const DuelSession& responderSession = suite->responder->manager->getSession();
    ASSERT_TRUE(responderSession.isOutcomeKnown());
    EXPECT_TRUE(initiatorSession.isRoundSuccess());
    EXPECT_TRUE(responderSession.isRoundSuccess());
    EXPECT_EQ(initiatorSession.getInitiatorScore(), 2);
    EXPECT_EQ(initiatorSession.getResponderScore(), 2);
    EXPECT_EQ(responderSession.getInitiatorScore(), 2);
    EXPECT_EQ(responderSession.getResponderScore(), 2);
}

inline void nodeNetworkIntegrationBothSidesAgreeOnFailure(NodeNetworkIntegrationSuite* suite) {
    suite->playRound(NODE_NETWORK_TEST_THREE_OF_FIVE);

    suite->initiator->manager->concludeRound();
    suite->responder->manager->concludeRound();

    EXPECT_FALSE(suite->initiator->manager->wasLastRoundSuccess());
    EXPECT_FALSE(suite->responder->manager->wasLastRoundSuccess());
    EXPECT_EQ(suite->initiator->manager->getLastRoundScore(), 3);
    EXPECT_EQ(suite->responder->manager->getLastRoundScore(), 3);
    EXPECT_EQ(suite->initiator->manager->getPendingRecordCount(), 0u);
    EXPECT_EQ(suite->responder->manager->getPendingRecordCount(), 0u);
}

inline void nodeNetworkIntegrationBothDevicesStoreIdenticalRecords(NodeNetworkIntegrationSuite* suite) {
    suite->playRound(NODE_NETWORK_TEST_FOUR_OF_FIVE);

    suite->initiator->manager->concludeRound();
    suite->responder->manager->concludeRound();

    QuickdrawMatchRecord initiatorRecord;
    QuickdrawMatchRecord responderRecord;
    ASSERT_TRUE(suite->initiator->manager->readPendingRecord(0, initiatorRecord));
    ASSERT_TRUE(suite->responder->manager->readPendingRecord(0, responderRecord));
    expectRecordsEqual(initiatorRecord, responderRecord);
    EXPECT_EQ(initiatorRecord.playerAId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(initiatorRecord.playerBId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(initiatorRecord.winnerPlayerId, initiatorRecord.playerAId);
    EXPECT_EQ(initiatorRecord.playerADrawTimeMs, 2u);
    EXPECT_EQ(initiatorRecord.playerBDrawTimeMs, 2u);
    EXPECT_EQ(initiatorRecord.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
}

inline void nodeNetworkIntegrationLostInputResultIsResent(NodeNetworkIntegrationSuite* suite) {
    NodeInputSequence sequence;
    for (NodeInput& step : sequence.inputs) {
        step = makeNodeInput(ButtonIdentifier::PRIMARY_BUTTON, DuelLinkSide::RESPONDER);
    }
    suite->syncAndBegin(sequence);
    suite->responder->dropFirst(NodeNetworkCommand::INPUT_RESULT);

    for (uint8_t step = 0; step < NODE_SEQUENCE_LENGTH; step++) {
        ASSERT_TRUE(suite->responder->manager->onLocalInputResult(step, true, 1));
        pumpNodeNetworkPackets(*suite->initiator, *suite->responder);
    }
    ASSERT_FALSE(suite->initiator->manager->getSession().hasStepResult(0));

    suite->advance(NODE_RELIABLE_RETRY_MS);

    EXPECT_TRUE(suite->initiator->manager->getSession().allStepsResolved());
    ASSERT_TRUE(suite->initiator->manager->finishRound());
    pumpNodeNetworkPackets(*suite->initiator, *suite->responder);
    EXPECT_EQ(suite->responder->manager->getSession().getResponderScore(), 5);
}

inline void nodeNetworkIntegrationLostOutcomeIsResent(NodeNetworkIntegrationSuite* suite) {
    suite->playSteps(NODE_NETWORK_TEST_FOUR_OF_FIVE);
    suite->initiator->dropFirst(NodeNetworkCommand::OUTCOME);
    ASSERT_TRUE(suite->initiator->manager->finishRound());
    pumpNodeNetworkPackets(*suite->initiator, *suite->responder);
    ASSERT_FALSE(suite->responder->manager->isOutcomeKnown());
    ASSERT_FALSE(suite->initiator->manager->getSession().isOutcomeDelivered());

    suite->advance(NODE_RELIABLE_RETRY_MS);

    ASSERT_TRUE(suite->responder->manager->isOutcomeKnown());
    EXPECT_TRUE(suite->responder->manager->getSession().isRoundSuccess());
    EXPECT_TRUE(suite->initiator->manager->getSession().isOutcomeDelivered());
    EXPECT_EQ(suite->initiator->wireless.getPendingReliableCount(), 0u);
}

inline void nodeNetworkIntegrationOutcomeNeverArrives(NodeNetworkIntegrationSuite* suite) {
    suite->playSteps(NODE_NETWORK_TEST_FOUR_OF_FIVE);
    suite->initiator->dropOutgoing = [](const NodeNetworkPacket& packet) {
        return packet.command == static_cast<uint8_t>(NodeNetworkCommand::OUTCOME);
    };
    ASSERT_TRUE(suite->initiator->manager->finishRound());

    for (uint8_t attempt = 0; attempt < NODE_RELIABLE_MAX_ATTEMPTS; attempt++) {
        suite->advance(NODE_RELIABLE_RETRY_MS);
    }

    EXPECT_TRUE(suite->initiator->manager->getSession().isOutcomeUndelivered());
    EXPECT_FALSE(suite->responder->manager->isOutcomeKnown());
}

inline void
nodeNetworkIntegrationLostOutcomeAcksSplitTheDevices(NodeNetworkIntegrationSuite* suite) {
    suite->playSteps(NODE_NETWORK_TEST_FOUR_OF_FIVE);
    suite->responder->dropOutgoing = [](const NodeNetworkPacket& packet) {
        return packet.command == static_cast<uint8_t>(NodeNetworkCommand::ACK);
    };
    ASSERT_TRUE(suite->initiator->manager->finishRound());

    for (uint8_t attempt = 0; attempt < NODE_RELIABLE_MAX_ATTEMPTS; attempt++) {
        suite->advance(NODE_RELIABLE_RETRY_MS);
    }

    EXPECT_TRUE(suite->responder->manager->isOutcomeKnown());
    EXPECT_TRUE(suite->initiator->manager->getSession().isOutcomeUndelivered());
}

inline void nodeNetworkIntegrationAbortReachesOpponent(NodeNetworkIntegrationSuite* suite) {
    suite->syncAndBegin(NodeNetworkIntegrationSuite::makeAlternatingSequence());

    suite->initiator->manager->abortRound();
    pumpNodeNetworkPackets(*suite->initiator, *suite->responder);

    EXPECT_TRUE(suite->initiator->manager->isAborted());
    EXPECT_TRUE(suite->responder->manager->isAborted());
    EXPECT_FALSE(suite->responder->manager->getSession().isActive());
    EXPECT_EQ(suite->initiator->manager->getFailedAttempts(), 0u);
    EXPECT_EQ(suite->responder->manager->getFailedAttempts(), 0u);
}

inline void nodeNetworkIntegrationThreeFailuresExhaustBothDevices(NodeNetworkIntegrationSuite* suite) {
    for (uint32_t attempt = 1; attempt <= MAX_FAILED_ATTEMPTS; attempt++) {
        suite->playRound(NODE_NETWORK_TEST_THREE_OF_FIVE);
        suite->initiator->manager->concludeRound();
        suite->responder->manager->concludeRound();
    }

    EXPECT_TRUE(suite->initiator->manager->hasExhaustedAttempts());
    EXPECT_TRUE(suite->responder->manager->hasExhaustedAttempts());
}

inline void nodeNetworkIntegrationCorrectsInitiatorWithoutValidTime(NodeNetworkIntegrationSuite* suite) {
    suite->initiator->clock.setUnixTimeMs(90000);

    suite->handshake();

    ASSERT_TRUE(suite->initiator->manager->isHandshakeComplete());
    ASSERT_TRUE(suite->responder->manager->isHandshakeComplete());
    EXPECT_EQ(suite->initiator->manager->getSession().getOccurredAtUnixMs(), NODE_NETWORK_TEST_RESPONDER_MS);
    EXPECT_EQ(suite->responder->manager->getSession().getOccurredAtUnixMs(), NODE_NETWORK_TEST_RESPONDER_MS);
}

inline void nodeNetworkIntegrationDualInitiatorsAbort(NodeNetworkIntegrationSuite* suite) {
    suite->responder->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);

    ASSERT_TRUE(suite->initiator->manager->startMatch(NODE_NETWORK_TEST_MAC_B));
    pumpNodeNetworkPackets(*suite->initiator, *suite->responder);

    EXPECT_FALSE(suite->initiator->manager->getSession().isActive());
    EXPECT_FALSE(suite->responder->manager->getSession().isActive());
}
