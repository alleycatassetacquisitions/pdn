#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "device-mock.hpp"
#include "device-player-codec-tests.hpp"
#include "utility-tests.hpp"

/** An FDN with one PDN Connected on its input jack and an HTTP client that records requests. */
class FdnProvisioningTests : public testing::Test {
public:
    /** Cables the PDN and captures every lookup the FDN queues. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(0);

        ON_CALL(*fdnDevice.mockHttpClient, queueRequest(testing::_))
        .WillByDefault([this](HttpRequest&& request) {
            queuedRequests.push_back(std::move(request));
            return true;
        });
        ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
        .WillByDefault(testing::Return(PeerCommsState::ENABLED));
        // As an FDN whose server address is HTTPS; a test of the other kind turns it off.
        ON_CALL(*fdnDevice.mockHttpClient, isResponseServerVerified())
        .WillByDefault(testing::Return(true));

        fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
        fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
        fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, pdnMac);
    }

    /** Releases the captured requests and the fake clock. */
    void TearDown() override {
        queuedRequests.clear();
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** Hands the FDN a provision request for @p pin as if @p fromMac had sent it. */
    void deliverRequest(const char* pin, const uint8_t* fromMac, uint8_t seqId = 0) {
        PlayerProvisionRequestPacket request{};
        request.seqId = seqId;
        strncpy(request.pin, pin, sizeof(request.pin) - 1);
        fdnDevice.mockPeerComms->triggerPacketCallback(fromMac, PktType::kPlayerProvisionRequest,
                                                       reinterpret_cast<const uint8_t*>(&request),
                                                       sizeof(request));
    }

    /** Records how each of @p fdnProv's lookups ends: the player's name, or why there is none. */
    void listenForResults(FdnProvisioningManager& fdnProv) {
        fdnProv.setOnLookupDone([this](ProvisionLookupResult result, const char* name) {
            if (result == ProvisionLookupResult::PROFILE_DELIVERED) {
                results.push_back(name != nullptr ? name : "<unnamed>");
            } else if (result == ProvisionLookupResult::KEY_NOT_ISSUED) {
                results.push_back("<no key>");
            } else {
                results.push_back("<failed>");
            }
        });
    }

    /** Records every frame the FDN hands its radio, and each provision reply among them. */
    void captureRadio() {
        ON_CALL(*fdnDevice.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(
        [this](const uint8_t*, PktType type, const uint8_t* data, const size_t length) {
            radioFrames.emplace_back(reinterpret_cast<const char*>(data), length);
            if (type == PktType::kPlayerProvision && length == sizeof(PlayerProvisionPacket)) {
                PlayerProvisionPacket reply{};
                memcpy(&reply, data, sizeof(reply));
                replies.push_back(reply);
            }
            return 1;
        });
    }

    /** Has the cabled PDN ask for @p pin and the server answer the lookup with @p body. */
    void answerLookup(const char* pin, const std::string& body) {
        const size_t requestIndex = queuedRequests.size();
        deliverRequest(pin, pdnMac, static_cast<uint8_t>(requestIndex + 1));
        ASSERT_EQ(queuedRequests.size(), requestIndex + 1);
        queuedRequests[requestIndex].onSuccess(body);
    }

    /** @return true when the last frame the FDN wrote to a jack is the test key, on the PDN's jack.
     */
    bool wroteTestKeyToPdnJack() const {
        if (fdnRdc.getSentApplicationFrames().empty()) return false;
        const std::pair<SerialIdentifier, ApplicationPayload>& sent =
        fdnRdc.getSentApplicationFrames().back();
        return sent.first == SerialIdentifier::INPUT_JACK &&
               std::string(sent.second.begin(), sent.second.end()) == testRecordKeyWire();
    }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    std::vector<HttpRequest> queuedRequests;
    std::vector<std::string> results;
    std::vector<std::string> radioFrames;
    std::vector<PlayerProvisionPacket> replies;
};

inline void fdnDropsMalformedOrUncabledProvisionRequest(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->deliverRequest("12/4", suite->pdnMac);
    EXPECT_TRUE(suite->queuedRequests.empty());

    const uint8_t strangerMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    suite->deliverRequest("1234", strangerMac);
    fdnProv.sync();
    EXPECT_TRUE(suite->queuedRequests.empty());
}

// The PDN sends on the tick its own jack reads Connected, which can be a tick
// before the FDN's does; the radio acks the request, so it is never sent twice.
inline void fdnServesRequestThatBeatItsJackToConnected(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    suite->deliverRequest("1234", suite->pdnMac);
    fdnProv.sync();
    ASSERT_TRUE(suite->queuedRequests.empty());

    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fdnProv.sync();
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "the request was dropped while the jack settled";
}

inline void fdnForgetsParkedRequestAfterItsWindow(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    suite->deliverRequest("1234", suite->pdnMac);
    suite->fakeClock->advance(2000);
    fdnProv.sync();

    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    fdnProv.sync();
    EXPECT_TRUE(suite->queuedRequests.empty()) << "a request was served long after it was sent";
}

inline void fdnCancelledLookupLeavesWifiAtOnce(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->deliverRequest("1234", suite->pdnMac);
    ASSERT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::WIFI);

    fdnProv.cancelLookup();
    EXPECT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW)
    << "the FDN stayed deaf to ESP-NOW for a PDN that is no longer there";

    // Leaving Wi-Fi fails the queued request, as the HTTP driver's disconnect does.
    suite->queuedRequests[0].onError({WirelessError::CONNECTION_FAILED, "Disconnected"});
    suite->deliverRequest("1234", suite->pdnMac);
    EXPECT_EQ(suite->queuedRequests.size(), 2u) << "the cancelled lookup blocks the next one";
}

inline void fdnSeesUnprovisionedPdnUntilItRegisters(FdnProvisioningTests* suite) {
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
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, registered.toProfile());
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

inline void fdnDropsLookupsWhileRegisteredPdnPlays(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    Player demo{};
    demo.applyRegistrationProfile(UUIDBytes(), "Demo", Team{}, {});
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, demo.toProfile());

    suite->deliverRequest("1234", suite->pdnMac);
    EXPECT_TRUE(suite->queuedRequests.empty()) << "no Wi-Fi excursion during symbol-match";

    fdnProv.setLookupDuringPlayAllowed(true);
    suite->deliverRequest("1234", suite->pdnMac);
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "the registration screen takes it";
}

inline void fdnReportsLookupResultAndResendsUnconfirmedReply(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    const std::vector<std::string>& results = suite->results;
    int replyFrames = 0;
    ON_CALL(*suite->fdnDevice.mockPeerComms,
            sendData(testing::_, PktType::kPlayerProvision, testing::_, testing::_))
    .WillByDefault([&replyFrames](const uint8_t*, PktType, const uint8_t*, const size_t) {
        replyFrames++;
        return 1;
    });

    suite->deliverRequest("0007", suite->pdnMac);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_TRUE(fdnProv.isLookupInFlight());

    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(results, (std::vector<std::string>{"Ada"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());
    EXPECT_EQ(replyFrames, 1);

    // No SEND_SUCCESS came back, so the reply goes out again on the backoff.
    suite->fakeClock->advance(150);
    suite->fdnDevice.wirelessManager->sync();
    EXPECT_EQ(replyFrames, 2) << "a reply the radio did not confirm is resent";

    suite->deliverRequest("0007", suite->pdnMac, 9);
    ASSERT_EQ(suite->queuedRequests.size(), 2u);
    suite->queuedRequests[1].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404"});
    EXPECT_EQ(results, (std::vector<std::string>{"Ada", "<failed>"}));
}

// The key crosses the jack and only the jack; the profile waits for the jack's word.
inline void fdnWritesKeyToJackThenSendsProfile(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    suite->captureRadio();

    suite->answerLookup("0007", encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    ASSERT_EQ(suite->fdnRdc.getSentApplicationFrames().size(), 1u);
    EXPECT_TRUE(suite->wroteTestKeyToPdnJack());

    // No timer stands in for the jack's report.
    suite->fakeClock->advance(PROVISION_LOOKUP_TIMEOUT_MS + 1);
    fdnProv.sync();
    suite->fdnDevice.wirelessManager->sync();
    EXPECT_TRUE(suite->replies.empty()) << "the profile left before the key frame was written";
    EXPECT_TRUE(suite->results.empty());
    EXPECT_TRUE(fdnProv.isLookupInFlight());

    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    ASSERT_EQ(suite->replies.size(), 1u) << "the profile did not follow the written key at once";
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
    EXPECT_STREQ(suite->replies[0].playerName, "Ada");
    EXPECT_STREQ(suite->replies[0].pin, "0007");
    EXPECT_EQ(suite->results, (std::vector<std::string>{"Ada"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());

    // Half the key is already too much of it to find on the radio, resends included.
    suite->fakeClock->advance(150);
    suite->fdnDevice.wirelessManager->sync();
    ASSERT_GE(suite->radioFrames.size(), 2u);
    const std::string keyHalf = testRecordKeyWire().substr(0, RECORD_KEY_SIZE / 2);
    for (const std::string& frame : suite->radioFrames) {
        EXPECT_EQ(frame.find(keyHalf), std::string::npos) << "the record key crossed the radio";
    }
}

// The server knows the player but issues no key the FDN accepts: the PDN is
// told the lookup failed and the listener hears that no key was issued.
inline void fdnLookupWithoutUsableKeyFailsRegistration(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    const std::string id = uuidWireBytes(TEST_PLAYER_ID);
    std::string shortKey = encodeTestPlayerWithoutKey(id, "Ada");
    appendTestRecordKey(shortKey, testRecordKeyWire(RECORD_KEY_SIZE - 1));
    std::string longKey = encodeTestPlayerWithoutKey(id, "Ada");
    appendTestRecordKey(longKey, testRecordKeyWire(RECORD_KEY_SIZE + 1));

    size_t lookups = 0;
    for (const std::string& body : {encodeTestPlayerWithoutKey(id, "Ada"), shortKey, longKey}) {
        suite->answerLookup("0007", body);
        lookups++;
        EXPECT_TRUE(suite->fdnRdc.getSentApplicationFrames().empty())
        << "lookup " << lookups << " put an unusable key on the jack";
        ASSERT_EQ(suite->replies.size(), lookups) << "the PDN was left waiting out its timeout";
        EXPECT_EQ(suite->replies.back().status,
                  static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));
        EXPECT_STREQ(suite->replies.back().playerName, "") << "a profile went out with no key";
        EXPECT_FALSE(fdnProv.isLookupInFlight());
    }
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>", "<no key>", "<no key>"}));
}

inline void fdnKeyFromUnverifiedLookupFailsRegistration(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    ON_CALL(*suite->fdnDevice.mockHttpClient, isResponseServerVerified())
    .WillByDefault(testing::Return(false));

    suite->answerLookup("0007", encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_TRUE(suite->fdnRdc.getSentApplicationFrames().empty())
    << "a key from an unverified server reached the jack";
    ASSERT_EQ(suite->replies.size(), 1u);
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
}

inline void fdnJackRefusingKeyFrameFailsRegistration(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    suite->fdnRdc.setApplicationFramesRefused(true);

    suite->answerLookup("0007", encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    ASSERT_EQ(suite->replies.size(), 1u) << "a still-Connected PDN was not told";
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());

    // The refusal is not remembered against the next attempt.
    suite->fdnRdc.setApplicationFramesRefused(false);
    suite->answerLookup("0007", encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    ASSERT_EQ(suite->replies.size(), 2u);
    EXPECT_EQ(suite->replies[1].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
}

// A PDN that is gone gets neither a profile nor a failure notice, at whichever
// point of the delivery it left.
inline void fdnSendsNothingToPdnThatLeftBeforeKeyWasWritten(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    const std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");
    FakeRemoteDeviceCoordinator& rdc = suite->fdnRdc;

    // Left while the frame waited for the writer: the jack reports it dropped.
    suite->answerLookup("0007", body);
    ASSERT_EQ(rdc.getSentApplicationFrames().size(), 1u);
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());

    // Left while the server was still answering: the key never reaches the jack.
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    suite->deliverRequest("0007", suite->pdnMac, 21);
    ASSERT_EQ(suite->queuedRequests.size(), 2u);
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    suite->queuedRequests[1].onSuccess(body);
    EXPECT_EQ(rdc.getSentApplicationFrames().size(), 1u) << "a key was written for a PDN that left";
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>", "<no key>"}));

    // Left mid-write: the jack reports the frame written, to a PDN that is gone.
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    suite->answerLookup("0007", body);
    ASSERT_EQ(rdc.getSentApplicationFrames().size(), 2u);
    rdc.beginApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>", "<no key>", "<no key>"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());

    EXPECT_TRUE(suite->replies.empty()) << "a PDN that left was sent a profile or a failure";
}

// Cancelling cannot recall a frame the jack already has, so the frame's late
// outcome must not release the profile of a lookup that was given up.
inline void fdnLookupCancelledDuringKeyHandOffSendsNoProfile(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    const std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");

    suite->answerLookup("0007", body);
    ASSERT_TRUE(fdnProv.isLookupInFlight());
    fdnProv.cancelLookup();
    EXPECT_FALSE(fdnProv.isLookupInFlight());
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
    ASSERT_EQ(suite->replies.size(), 1u) << "a still-Connected PDN was not told";
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));

    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(suite->replies.size(), 1u) << "the cancelled lookup's profile followed its key";
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));

    suite->answerLookup("0007", body);
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    ASSERT_EQ(suite->replies.size(), 2u) << "the cancelled lookup blocks the next one";
    EXPECT_EQ(suite->replies[1].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
}

// On a device giving Wi-Fi up fails the request; an answer that arrives anyway
// belongs to a lookup that is over.
inline void fdnAnswerToCancelledLookupWritesNoKey(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->listenForResults(fdnProv);
    suite->captureRadio();

    suite->deliverRequest("0007", suite->pdnMac, 1);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    fdnProv.cancelLookup();
    ASSERT_EQ(suite->replies.size(), 1u);

    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_TRUE(suite->fdnRdc.getSentApplicationFrames().empty())
    << "a cancelled lookup's key was written to the jack";
    EXPECT_FALSE(fdnProv.isLookupInFlight());
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<failed>"}));
    EXPECT_EQ(suite->replies.size(), 1u);
}

// Cables a second PDN to the FDN's other input jack.
inline void cableSecondPdn(FdnProvisioningTests* suite, const uint8_t* secondPdnMac) {
    suite->fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK_SECONDARY, DeviceType::PDN);
    suite->fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK_SECONDARY, secondPdnMac);
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK_SECONDARY, JackStatus::CONNECTED);
}

// With two PDNs cabled, the key is for the one that asked: it goes to that
// PDN's jack, and only that jack's report releases the profile.
inline void fdnWritesKeyOnlyToTheJackOfThePdnThatAsked(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->captureRadio();
    const uint8_t secondPdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    cableSecondPdn(suite, secondPdnMac);

    suite->deliverRequest("0007", secondPdnMac, 1);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    ASSERT_EQ(suite->fdnRdc.getSentApplicationFrames().size(), 1u);
    EXPECT_EQ(suite->fdnRdc.getSentApplicationFrames()[0].first,
              SerialIdentifier::INPUT_JACK_SECONDARY)
    << "the key went to the jack of a PDN that did not ask for it";

    // Some other frame finishes on the first PDN's jack.
    ApplicationPayload unrelated{};
    ASSERT_TRUE(suite->fdnRdc.sendApplicationFrame(SerialIdentifier::INPUT_JACK, unrelated));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    EXPECT_TRUE(suite->replies.empty()) << "another jack's report released the profile";
    EXPECT_TRUE(fdnProv.isLookupInFlight());

    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK_SECONDARY);
    ASSERT_EQ(suite->replies.size(), 1u);
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
}

// The radio is back on peer-to-peer while the first PDN's key is on its way to
// the jack, so a second PDN's request can land then. It is acked by the radio
// and never sent again, so it must be served, not dropped.
inline void fdnServesRequestThatArrivedDuringAnotherPdnsKeyHandOff(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc);
    suite->captureRadio();
    const uint8_t secondPdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    cableSecondPdn(suite, secondPdnMac);

    suite->answerLookup("0007", encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    ASSERT_TRUE(fdnProv.isLookupInFlight());
    suite->deliverRequest("0042", secondPdnMac, 9);
    fdnProv.sync();
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "a second lookup started under the first";

    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    ASSERT_EQ(suite->replies.size(), 1u);
    fdnProv.sync();
    ASSERT_EQ(suite->queuedRequests.size(), 2u) << "the second PDN's request was lost";
    EXPECT_EQ(suite->queuedRequests[1].path, "/device/players/by-pin/0042");

    // A PDN asking again while its own lookup runs is not a second lookup.
    suite->deliverRequest("0042", secondPdnMac, 10);
    suite->queuedRequests[1].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404"});
    fdnProv.sync();
    EXPECT_EQ(suite->queuedRequests.size(), 2u) << "a PDN's repeated request was looked up twice";
}
