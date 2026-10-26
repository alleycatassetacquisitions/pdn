#pragma once

#include <gtest/gtest.h>

#include <array>

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
        manager = new MatchManagerV2(&provider, &backingStorage, &wireless, &rdc, &clock);
    }

    ~NodeNetworkDevice() {
        delete manager;
        manager = nullptr;
    }

    NodeNetworkDevice(const NodeNetworkDevice&) = delete;
    NodeNetworkDevice& operator=(const NodeNetworkDevice&) = delete;

    /** Delivers every queued outgoing packet from this device to @p other. */
    bool deliverTo(NodeNetworkDevice& other) {
        bool delivered = false;
        while (!wireless.sent.empty()) {
            const FakeNodeNetworkWirelessManager::SentPacket next = wireless.sent.front();
            wireless.sent.erase(wireless.sent.begin());
            other.manager->onPacket(mac.data(), next.packet);
            delivered = true;
        }
        return delivered;
    }

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
        accept.matchId = manager->getSession().getMatchId();
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

    /** Remote draw as seen from the scripted peer (opposite side of the local device). */
    void deliverRemoteDraw(uint64_t timeMs) {
        const bool peerIsInitiator = manager->getSession().getLocalSide() == DuelLinkSide::RESPONDER;
        NodeNetworkPacket draw = makeNodeNetworkPacket(NodeNetworkCommand::DRAW, peerIsInitiator);
        draw.matchId = manager->getSession().getMatchId();
        draw.drawTimeMs = timeMs;
        manager->onPacket(DUEL_TEST_PEER_MAC, draw);
    }

    /** Completes a duel as initiator with both draw times. */
    void completeDuelAsInitiator(uint64_t localMs, uint64_t remoteMs) {
        completeHandshakeAsInitiator();
        ASSERT_TRUE(manager->onLocalDrawTimeMs(localMs));
        deliverRemoteDraw(remoteMs);
    }

    NodeNetworkDevice* device = nullptr;
    MatchManagerV2* manager = nullptr;
};

// ============================================
// startMatch
// ============================================

inline void matchManagerV2StartMatchSendsOffer(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);

    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& sent = suite->device->wireless.sent[0];
    EXPECT_EQ(memcmp(sent.destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
    EXPECT_EQ(sent.packet.protocolVersion, NODE_NETWORK_PROTOCOL_VERSION);
    EXPECT_EQ(sent.packet.command, NodeNetworkCommand::OFFER);
    EXPECT_EQ(sent.packet.senderIsInitiator, 1);
    EXPECT_EQ(sent.packet.senderPlayerId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(sent.packet.senderTeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
    EXPECT_EQ(sent.packet.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_FALSE(sent.packet.matchId.isZero());
    EXPECT_EQ(sent.packet.matchId, suite->manager->getSession().getMatchId());
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
    accept.matchId = suite->manager->getSession().getMatchId();

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
    accept.matchId = suite->manager->getSession().getMatchId();

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, accept);

    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2InitiatorIgnoresAcceptWithNilPlayerId(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket accept = makeNodeNetworkPacket(NodeNetworkCommand::ACCEPT, false);
    accept.matchId = suite->manager->getSession().getMatchId();
    accept.senderPlayerId = UUIDBytes();

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, accept);

    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

inline void matchManagerV2RoleMismatchFromOpponentClearsSession(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket mismatch = makeNodeNetworkPacket(NodeNetworkCommand::ROLE_MISMATCH, false);
    mismatch.matchId = suite->manager->getSession().getMatchId();

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, mismatch);

    EXPECT_FALSE(suite->manager->getSession().isActive());
}

inline void matchManagerV2RoleMismatchFromStrangerIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket mismatch = makeNodeNetworkPacket(NodeNetworkCommand::ROLE_MISMATCH, false);
    mismatch.matchId = suite->manager->getSession().getMatchId();

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
    EXPECT_EQ(session.getMatchId(), offer.matchId);
    EXPECT_EQ(session.getOccurredAtUnixMs(), NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_EQ(session.getOpponentParticipant().playerId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(session.getOpponentParticipant().teamId, makeTestUuid(DUEL_TEST_PEER_TEAM));
    EXPECT_TRUE(suite->manager->isHandshakeComplete());

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& reply = suite->device->wireless.sent[0];
    EXPECT_EQ(memcmp(reply.destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
    EXPECT_EQ(reply.packet.command, NodeNetworkCommand::ACCEPT);
    EXPECT_EQ(reply.packet.senderIsInitiator, 0);
    EXPECT_EQ(reply.packet.matchId, offer.matchId);
    EXPECT_EQ(reply.packet.senderPlayerId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(reply.packet.senderTeamId, makeTestUuid(DUEL_TEST_LOCAL_TEAM));
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
    offer.senderPlayerId = UUIDBytes();

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
    second.matchId = makeTestUuid(DUEL_TEST_PEER_ID);
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
    EXPECT_EQ(suite->device->wireless.sent[0].packet.command, NodeNetworkCommand::ROLE_MISMATCH);
    EXPECT_EQ(suite->device->wireless.sent[0].packet.matchId, offer.matchId);
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
    EXPECT_EQ(reply.packet.command, NodeNetworkCommand::TIME_CORRECTION);
    EXPECT_EQ(reply.packet.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
    EXPECT_EQ(reply.packet.matchId, offer.matchId);
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
    correction.matchId = matchId;
    correction.occurredAtUnixMs = NODE_NETWORK_TEST_RESPONDER_MS;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, correction);

    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& resent = suite->device->wireless.sent[0];
    EXPECT_EQ(resent.packet.command, NodeNetworkCommand::OFFER);
    EXPECT_EQ(resent.packet.matchId, matchId);
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
    correction.matchId = suite->manager->getSession().getMatchId();
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
    correction.matchId = suite->manager->getSession().getMatchId();
    correction.occurredAtUnixMs = 0;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, correction);

    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

// ============================================
// Draw exchange
// ============================================

inline void matchManagerV2LocalDrawIsRefusedBeforeHandshake(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    suite->device->wireless.sent.clear();

    EXPECT_FALSE(suite->manager->onLocalDrawTimeMs(215));

    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2LocalDrawRecordsAndSends(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    suite->device->wireless.sent.clear();

    EXPECT_TRUE(suite->manager->onLocalDrawTimeMs(215));

    EXPECT_EQ(suite->manager->getSession().getInitiatorDrawTimeMs(), 215u);
    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    const FakeNodeNetworkWirelessManager::SentPacket& sent = suite->device->wireless.sent[0];
    EXPECT_EQ(sent.packet.command, NodeNetworkCommand::DRAW);
    EXPECT_EQ(sent.packet.senderIsInitiator, 1);
    EXPECT_EQ(sent.packet.drawTimeMs, 215u);
    EXPECT_EQ(sent.packet.matchId, suite->manager->getSession().getMatchId());
    EXPECT_EQ(memcmp(sent.destinationMac.data(), DUEL_TEST_PEER_MAC, 6), 0);
}

inline void matchManagerV2ResponderDrawUsesResponderSide(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsResponder();
    suite->device->wireless.sent.clear();

    EXPECT_TRUE(suite->manager->onLocalDrawTimeMs(300));

    EXPECT_EQ(suite->manager->getSession().getResponderDrawTimeMs(), 300u);
    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    EXPECT_EQ(suite->device->wireless.sent[0].packet.senderIsInitiator, 0);
    EXPECT_EQ(suite->device->wireless.sent[0].packet.drawTimeMs, 300u);
}

inline void matchManagerV2SecondLocalDrawIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    ASSERT_TRUE(suite->manager->onLocalDrawTimeMs(215));
    suite->device->wireless.sent.clear();

    EXPECT_FALSE(suite->manager->onLocalDrawTimeMs(999));

    EXPECT_EQ(suite->manager->getSession().getInitiatorDrawTimeMs(), 215u);
    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

inline void matchManagerV2RemoteDrawFillsOppositeSlot(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();

    suite->deliverRemoteDraw(260);

    EXPECT_TRUE(suite->manager->getSession().hasResponderDrawTime());
    EXPECT_EQ(suite->manager->getSession().getResponderDrawTimeMs(), 260u);
}

inline void matchManagerV2DuplicateRemoteDrawIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    suite->deliverRemoteDraw(260);

    suite->deliverRemoteDraw(999);

    EXPECT_EQ(suite->manager->getSession().getResponderDrawTimeMs(), 260u);
}

inline void matchManagerV2RemoteDrawClaimingOwnSideIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    NodeNetworkPacket draw = makeNodeNetworkPacket(NodeNetworkCommand::DRAW, true);
    draw.matchId = suite->manager->getSession().getMatchId();
    draw.drawTimeMs = 260;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, draw);

    EXPECT_FALSE(suite->manager->getSession().hasResponderDrawTime());
    EXPECT_FALSE(suite->manager->getSession().hasInitiatorDrawTime());
}

inline void matchManagerV2RemoteDrawForOtherMatchIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    NodeNetworkPacket draw = makeNodeNetworkPacket(NodeNetworkCommand::DRAW, false);
    draw.drawTimeMs = 260;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, draw);

    EXPECT_FALSE(suite->manager->getSession().hasResponderDrawTime());
}

inline void matchManagerV2RemoteDrawFromStrangerIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    NodeNetworkPacket draw = makeNodeNetworkPacket(NodeNetworkCommand::DRAW, false);
    draw.matchId = suite->manager->getSession().getMatchId();
    draw.drawTimeMs = 260;

    suite->manager->onPacket(DUEL_TEST_OTHER_MAC, draw);

    EXPECT_FALSE(suite->manager->getSession().hasResponderDrawTime());
}

inline void matchManagerV2RemoteDrawBeforeHandshakeIsIgnored(MatchManagerV2TestSuite* suite) {
    suite->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
    ASSERT_TRUE(suite->manager->startMatch(DUEL_TEST_PEER_MAC));
    NodeNetworkPacket draw = makeNodeNetworkPacket(NodeNetworkCommand::DRAW, false);
    draw.matchId = suite->manager->getSession().getMatchId();
    draw.drawTimeMs = 260;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, draw);

    EXPECT_FALSE(suite->manager->getSession().hasResponderDrawTime());
}

inline void matchManagerV2RemoteNeverPressedRecordsTimeAndFlag(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    ASSERT_TRUE(suite->manager->onLocalDrawTimeMs(215));
    NodeNetworkPacket never = makeNodeNetworkPacket(NodeNetworkCommand::NEVER_PRESSED, false);
    never.matchId = suite->manager->getSession().getMatchId();
    never.drawTimeMs = 4000;

    suite->manager->onPacket(DUEL_TEST_PEER_MAC, never);

    EXPECT_TRUE(suite->manager->getSession().didOpponentNeverPress());
    EXPECT_EQ(suite->manager->getSession().getResponderDrawTimeMs(), 4000u);
    EXPECT_TRUE(suite->manager->isExchangeComplete());
    EXPECT_FALSE(suite->manager->didSync());
}

inline void matchManagerV2LocalGraceExpirySendsNeverPressed(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    suite->device->wireless.sent.clear();

    EXPECT_TRUE(suite->manager->onLocalGraceExpired(4000));

    EXPECT_TRUE(suite->manager->getSession().didGraceExpireWithoutPress());
    EXPECT_TRUE(suite->manager->isExchangeComplete());
    ASSERT_EQ(suite->device->wireless.sent.size(), 1u);
    EXPECT_EQ(suite->device->wireless.sent[0].packet.command, NodeNetworkCommand::NEVER_PRESSED);
    EXPECT_EQ(suite->device->wireless.sent[0].packet.senderIsInitiator, 1);
    EXPECT_EQ(suite->device->wireless.sent[0].packet.drawTimeMs, 4000u);
    EXPECT_FALSE(suite->manager->didSync());
}

inline void matchManagerV2LocalGraceExpiryIsRefusedAfterPress(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    ASSERT_TRUE(suite->manager->onLocalDrawTimeMs(215));
    suite->device->wireless.sent.clear();

    EXPECT_FALSE(suite->manager->onLocalGraceExpired(4000));

    EXPECT_TRUE(suite->device->wireless.sent.empty());
}

// ============================================
// Sync decision
// ============================================

inline void matchManagerV2SyncsInsideWindow(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 350);

    EXPECT_TRUE(suite->manager->isExchangeComplete());
    EXPECT_TRUE(suite->manager->didSync());
}

inline void matchManagerV2SyncsAtExactWindowEdge(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 200 + NODE_SYNC_WINDOW_MS);

    EXPECT_TRUE(suite->manager->didSync());
}

inline void matchManagerV2SyncsAtWindowEdgeEitherDirection(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200 + NODE_SYNC_WINDOW_MS, 200);

    EXPECT_TRUE(suite->manager->didSync());
}

inline void matchManagerV2FailsJustOutsideWindow(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 201 + NODE_SYNC_WINDOW_MS);

    EXPECT_TRUE(suite->manager->isExchangeComplete());
    EXPECT_FALSE(suite->manager->didSync());
}

inline void matchManagerV2FailsFarOutsideWindow(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 500);

    EXPECT_FALSE(suite->manager->didSync());
}

inline void matchManagerV2DoesNotSyncWithoutRemoteTime(MatchManagerV2TestSuite* suite) {
    suite->completeHandshakeAsInitiator();
    ASSERT_TRUE(suite->manager->onLocalDrawTimeMs(200));

    EXPECT_FALSE(suite->manager->isExchangeComplete());
    EXPECT_FALSE(suite->manager->didSync());
}

inline void matchManagerV2DoesNotSyncWithoutSession(MatchManagerV2TestSuite* suite) {
    EXPECT_FALSE(suite->manager->didSync());
    EXPECT_FALSE(suite->manager->isExchangeComplete());
    EXPECT_FALSE(suite->manager->isHandshakeComplete());
}

// ============================================
// Finalize
// ============================================

inline void matchManagerV2FinalizeStoresRecordAndClearsSession(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 240);

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
    EXPECT_EQ(record.playerADrawTimeMs, 200u);
    EXPECT_EQ(record.playerBDrawTimeMs, 240u);
    EXPECT_EQ(record.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
}

inline void matchManagerV2FinalizeRefusedWhenNotSynced(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 900);

    EXPECT_FALSE(suite->manager->finalizeMatch());

    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
    EXPECT_TRUE(suite->manager->getSession().isActive());
}

inline void matchManagerV2FinalizeRefusedWithoutSession(MatchManagerV2TestSuite* suite) {
    EXPECT_FALSE(suite->manager->finalizeMatch());
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
}

inline void matchManagerV2FinalizeRefusedWhenStorageWriteFails(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 240);
    suite->device->backingStorage.failWrites = true;

    EXPECT_FALSE(suite->manager->finalizeMatch());

    suite->device->backingStorage.failWrites = false;
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 0u);
    EXPECT_TRUE(suite->manager->getSession().isActive());
}

inline void matchManagerV2ClearCurrentMatchLeavesStorageAlone(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 240);
    ASSERT_TRUE(suite->manager->finalizeMatch());
    suite->completeDuelAsInitiator(200, 240);

    suite->manager->clearCurrentMatch();

    EXPECT_FALSE(suite->manager->getSession().isActive());
    EXPECT_EQ(suite->manager->getPendingRecordCount(), 1u);
}

inline void matchManagerV2ClearPendingRecordsEmptiesStorage(MatchManagerV2TestSuite* suite) {
    suite->completeDuelAsInitiator(200, 240);
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

    suite->completeDuelAsInitiator(200, 240);
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
    suite->completeDuelAsInitiator(200, 240);
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
    expected.playerADrawTimeMs = 200;
    expected.playerBDrawTimeMs = 240;
    expected.occurredAtUnixMs = NODE_NETWORK_TEST_INITIATOR_MS;
    expectRecordsEqual(expected, record);
}

// ============================================
// Two-device integration
// ============================================

class NodeNetworkIntegrationSuite : public testing::Test {
public:
    void SetUp() override {
        initiator = new NodeNetworkDevice(DUEL_TEST_LOCAL_ID, DUEL_TEST_LOCAL_TEAM,
                                        NODE_NETWORK_TEST_MAC_A, NODE_NETWORK_TEST_MAC_B,
                                        NODE_NETWORK_TEST_INITIATOR_MS);
        responder = new NodeNetworkDevice(DUEL_TEST_PEER_ID, DUEL_TEST_PEER_TEAM,
                                        NODE_NETWORK_TEST_MAC_B, NODE_NETWORK_TEST_MAC_A,
                                        NODE_NETWORK_TEST_RESPONDER_MS);
        initiator->manager->setLocalLinkSide(DuelLinkSide::INITIATOR);
        responder->manager->setLocalLinkSide(DuelLinkSide::RESPONDER);
    }

    void TearDown() override {
        delete initiator;
        delete responder;
        initiator = nullptr;
        responder = nullptr;
    }

    /** Runs offer/accept so both devices hold a complete handshake. */
    void handshake() {
        ASSERT_TRUE(initiator->manager->startMatch(NODE_NETWORK_TEST_MAC_B));
        pumpNodeNetworkPackets(*initiator, *responder);
    }

    /** Both players press; packets flow both ways. */
    void duel(uint64_t initiatorMs, uint64_t responderMs) {
        handshake();
        ASSERT_TRUE(initiator->manager->onLocalDrawTimeMs(initiatorMs));
        ASSERT_TRUE(responder->manager->onLocalDrawTimeMs(responderMs));
        pumpNodeNetworkPackets(*initiator, *responder);
    }

    NodeNetworkDevice* initiator = nullptr;
    NodeNetworkDevice* responder = nullptr;
};

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

inline void nodeNetworkIntegrationBothSidesAgreeOnSync(NodeNetworkIntegrationSuite* suite) {
    suite->duel(200, 380);

    EXPECT_TRUE(suite->initiator->manager->isExchangeComplete());
    EXPECT_TRUE(suite->responder->manager->isExchangeComplete());
    EXPECT_TRUE(suite->initiator->manager->didSync());
    EXPECT_TRUE(suite->responder->manager->didSync());
}

inline void nodeNetworkIntegrationBothSidesAgreeOnFailure(NodeNetworkIntegrationSuite* suite) {
    suite->duel(200, 700);

    EXPECT_FALSE(suite->initiator->manager->didSync());
    EXPECT_FALSE(suite->responder->manager->didSync());
    EXPECT_FALSE(suite->initiator->manager->finalizeMatch());
    EXPECT_FALSE(suite->responder->manager->finalizeMatch());
    EXPECT_EQ(suite->initiator->manager->getPendingRecordCount(), 0u);
    EXPECT_EQ(suite->responder->manager->getPendingRecordCount(), 0u);
}

inline void nodeNetworkIntegrationBothDevicesStoreIdenticalRecords(NodeNetworkIntegrationSuite* suite) {
    suite->duel(200, 380);

    ASSERT_TRUE(suite->initiator->manager->finalizeMatch());
    ASSERT_TRUE(suite->responder->manager->finalizeMatch());

    QuickdrawMatchRecord initiatorRecord;
    QuickdrawMatchRecord responderRecord;
    ASSERT_TRUE(suite->initiator->manager->readPendingRecord(0, initiatorRecord));
    ASSERT_TRUE(suite->responder->manager->readPendingRecord(0, responderRecord));
    expectRecordsEqual(initiatorRecord, responderRecord);
    EXPECT_EQ(initiatorRecord.playerAId, makeTestUuid(DUEL_TEST_LOCAL_ID));
    EXPECT_EQ(initiatorRecord.playerBId, makeTestUuid(DUEL_TEST_PEER_ID));
    EXPECT_EQ(initiatorRecord.winnerPlayerId, initiatorRecord.playerAId);
    EXPECT_EQ(initiatorRecord.playerADrawTimeMs, 200u);
    EXPECT_EQ(initiatorRecord.playerBDrawTimeMs, 380u);
    EXPECT_EQ(initiatorRecord.occurredAtUnixMs, NODE_NETWORK_TEST_INITIATOR_MS);
}

inline void nodeNetworkIntegrationNeverPressedFailsBothSides(NodeNetworkIntegrationSuite* suite) {
    suite->handshake();
    ASSERT_TRUE(suite->initiator->manager->onLocalDrawTimeMs(200));
    ASSERT_TRUE(suite->responder->manager->onLocalGraceExpired(4000));
    pumpNodeNetworkPackets(*suite->initiator, *suite->responder);

    EXPECT_TRUE(suite->initiator->manager->isExchangeComplete());
    EXPECT_TRUE(suite->responder->manager->isExchangeComplete());
    EXPECT_FALSE(suite->initiator->manager->didSync());
    EXPECT_FALSE(suite->responder->manager->didSync());
}

inline void nodeNetworkIntegrationThreeFailuresExhaustBothDevices(NodeNetworkIntegrationSuite* suite) {
    for (uint32_t attempt = 1; attempt <= MAX_FAILED_ATTEMPTS; attempt++) {
        suite->duel(200, 900);
        suite->initiator->manager->recordLinkOutcome(false);
        suite->responder->manager->recordLinkOutcome(false);
        suite->initiator->manager->clearCurrentMatch();
        suite->responder->manager->clearCurrentMatch();
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
