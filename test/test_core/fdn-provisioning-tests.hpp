#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <initializer_list>

#include "alleycat-protobuf-test-helpers.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "device-mock.hpp"
#include "device-player-codec-tests.hpp"
#include "fake-band-reader.hpp"
#include "utility-tests.hpp"

/**
 * An FDN with an unregistered PDN Connected on its primary jack, a band reader,
 * and an HTTP client that records requests.
 */
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

        cablePdn(SerialIdentifier::INPUT_JACK, pdnMac);
    }

    /** Releases the captured requests and the fake clock. */
    void TearDown() override {
        queuedRequests.clear();
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** Connects an unregistered PDN with @p mac on @p jack. */
    void cablePdn(SerialIdentifier jack, const uint8_t* mac) {
        PlayerProfile unregistered{};
        unregistered.userId = 0xFFFF;
        fdnRdc.setPeerProfile(jack, unregistered);
        fdnRdc.setPeerMac(jack, mac);
        fdnRdc.setJackStatus(jack, JackStatus::CONNECTED);
    }

    /** Lets @p fdnProv see the jacks, then taps a band with @p uid while the FDN polls. */
    void tapBand(FdnProvisioningManager& fdnProv,
                 std::initializer_list<uint8_t> uid = {0x04, 0xA2, 0x1B, 0x9C}) {
        fdnProv.sync();
        reader.placeBand(uid);
        fdnProv.sync();
    }

    /** Records how each of @p fdnProv's lookups ends: the player's name, or why there is none. */
    void listenForResults(FdnProvisioningManager& fdnProv) {
        fdnProv.setOnLookupDone(
        [this](ProvisionLookupResult result, SerialIdentifier jack, const char* name) {
            resultJacks.push_back(jack);
            if (result == ProvisionLookupResult::PROFILE_DELIVERED) {
                results.push_back(name != nullptr ? name : "<unnamed>");
            } else if (result == ProvisionLookupResult::NO_PLAYER) {
                results.push_back("<not found>");
            } else if (result == ProvisionLookupResult::SERVER_UNREACHABLE) {
                results.push_back("<unreachable>");
            } else if (result == ProvisionLookupResult::KEY_NOT_ISSUED) {
                results.push_back("<no key>");
            } else if (result == ProvisionLookupResult::SERVER_NOT_TRUSTED) {
                results.push_back("<not trusted>");
            } else {
                results.push_back("<failed>");
            }
        });
    }

    /** Records every frame the FDN hands its radio, and each provision reply among them. */
    void captureRadio() {
        ON_CALL(*fdnDevice.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(
        [this](const uint8_t* toMac, PktType type, const uint8_t* data, const size_t length) {
            radioFrames.emplace_back(reinterpret_cast<const char*>(data), length);
            if (type == PktType::kPlayerProvision && length == sizeof(PlayerProvisionPacket)) {
                PlayerProvisionPacket reply{};
                memcpy(&reply, data, sizeof(reply));
                replies.push_back(reply);
                std::array<uint8_t, 6> mac{};
                memcpy(mac.data(), toMac, mac.size());
                replyMacs.push_back(mac);
            }
            return 1;
        });
    }

    /** Taps a band for the pending registration and has the server answer with @p body. */
    void answerLookup(FdnProvisioningManager& fdnProv, const std::string& body) {
        const size_t requestIndex = queuedRequests.size();
        tapBand(fdnProv);
        ASSERT_EQ(queuedRequests.size(), requestIndex + 1);
        queuedRequests[requestIndex].onSuccess(body);
    }

    /** Lets the result of @p fdnProv's last lookup run out, so the next registration is pending. */
    void waitOutLookupResult(FdnProvisioningManager& fdnProv) {
        fakeClock->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS + 1);
        fdnProv.sync();
    }

    /** @return true when the last frame the FDN wrote to a jack is the test key, on @p jack. */
    bool wroteTestKeyTo(SerialIdentifier jack) const {
        if (fdnRdc.getSentApplicationFrames().empty()) return false;
        const std::pair<SerialIdentifier, ApplicationPayload>& sent =
        fdnRdc.getSentApplicationFrames().back();
        return sent.first == jack &&
               std::string(sent.second.begin(), sent.second.end()) == testRecordKeyWire();
    }

    /** @return true when reply @p index went to @p mac. */
    bool replyWentTo(size_t index, const uint8_t* mac) const {
        return index < replyMacs.size() && memcmp(replyMacs[index].data(), mac, 6) == 0;
    }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    FakeBandReader reader;
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t secondPdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    std::vector<HttpRequest> queuedRequests;
    std::vector<std::string> results;
    std::vector<SerialIdentifier> resultJacks;
    std::vector<std::string> radioFrames;
    std::vector<PlayerProvisionPacket> replies;
    std::vector<std::array<uint8_t, 6>> replyMacs;
};

// ---- The registration queue ----

// Arrival order, not jack order: the secondary jack's PDN came first.
inline void fdnQueuesRegistrationsInArrivalOrder(FdnProvisioningTests* suite) {
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    fdnProv.sync();
    suite->cablePdn(SerialIdentifier::INPUT_JACK, suite->pdnMac);
    fdnProv.sync();
    ASSERT_TRUE(fdnProv.hasPendingRegistration());
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK_SECONDARY);

    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_FALSE(fdnProv.hasPendingRegistration()) << "a registration was pending under a lookup";
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK_SECONDARY);
    EXPECT_EQ(suite->results, (std::vector<std::string>{"Ada"}));
    fdnProv.sync();
    EXPECT_FALSE(fdnProv.hasPendingRegistration()) << "a registration was pending under the result";
    suite->waitOutLookupResult(fdnProv);
    ASSERT_TRUE(fdnProv.hasPendingRegistration());
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK)
    << "the queued PDN did not get its turn when the first registration ended";
}

inline void fdnSendsUntappedRegistrationToTheBackOfTheQueue(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    fdnProv.sync();
    ASSERT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK);

    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK) << "it ended early";
    suite->fakeClock->advance(1);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK_SECONDARY);

    // The turn just begun gets its full time, and the first PDN, still cabled, comes back.
    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK_SECONDARY);
    suite->fakeClock->advance(1);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK);

    // Alone in the queue, a timed-out PDN is at the back and the front at once.
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK_SECONDARY, JackStatus::DISCONNECTED);
    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 1);
    fdnProv.sync();
    ASSERT_TRUE(fdnProv.hasPendingRegistration());
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK);
    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "a requeued registration took no tap";
}

inline void fdnDropsUnpluggedPdnFromTheQueue(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    fdnProv.sync();

    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK_SECONDARY, JackStatus::DISCONNECTED);
    fdnProv.sync();
    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 1);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK)
    << "the turn passed to a PDN that had left";

    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    suite->tapBand(fdnProv);
    EXPECT_FALSE(fdnProv.hasPendingRegistration());
    EXPECT_TRUE(suite->queuedRequests.empty()) << "a tap was looked up for a PDN that had left";

    // Plugged back in, it queues as a new arrival.
    suite->cablePdn(SerialIdentifier::INPUT_JACK, suite->pdnMac);
    fdnProv.sync();
    EXPECT_TRUE(fdnProv.hasPendingRegistration());
}

// When a delivery ends, the PDN's context still shows no applied profile until
// it applies the profile and resends it; until then it is queued again.
inline void fdnRequeuesADeliveredPdnUntilItShowsAnAppliedProfile(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    suite->waitOutLookupResult(fdnProv);
    ASSERT_TRUE(fdnProv.hasPendingRegistration())
    << "a delivered PDN that has not applied its profile was not requeued";
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK);

    Player registered{};
    UUIDBytes playerId{};
    UUIDBytes::parse(TEST_PLAYER_ID, playerId);
    registered.applyRegistrationProfile(playerId, "Ada", Team{}, {});
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, registered.toProfile());
    fdnProv.sync();
    EXPECT_FALSE(fdnProv.hasPendingRegistration())
    << "a PDN showing an applied profile stayed queued";
    EXPECT_FALSE(fdnProv.hasUnprovisionedPdnCabled());
}

inline void fdnSeesUnprovisionedPdnUntilItRegisters(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    EXPECT_TRUE(fdnProv.hasUnprovisionedPdnCabled());
    EXPECT_FALSE(fdnProv.hasRegisteredPdnCabled());

    Player registered{};
    UUIDBytes playerId{};
    UUIDBytes::parse(TEST_PLAYER_ID, playerId);
    registered.applyRegistrationProfile(playerId, "Ada", Team{}, {});
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, registered.toProfile());
    EXPECT_FALSE(fdnProv.hasUnprovisionedPdnCabled());
    fdnProv.sync();
    EXPECT_FALSE(fdnProv.hasPendingRegistration()) << "a registered PDN was queued";

    Player demo{};
    Team hunter{};
    strncpy(hunter.name, "Hunter", sizeof(hunter.name) - 1);
    demo.applyRegistrationProfile(UUIDBytes(), "Demo", hunter, {});
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, demo.toProfile());
    EXPECT_FALSE(fdnProv.hasUnprovisionedPdnCabled())
    << "a demo player has no server id but is not waiting to register";
    EXPECT_TRUE(fdnProv.hasRegisteredPdnCabled()) << "and it keeps the symbol-match game open";
    fdnProv.sync();
    EXPECT_FALSE(fdnProv.hasPendingRegistration()) << "a demo PDN was queued";

    PlayerProfile unregistered{};
    unregistered.userId = 0xFFFF;
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    suite->fdnRdc.setPeerProfile(SerialIdentifier::INPUT_JACK, unregistered);
    EXPECT_FALSE(fdnProv.hasUnprovisionedPdnCabled()) << "only a Connected jack counts";
}

// ---- Taps ----

// A band held at the reader before anything was pending is one tap, already
// spent: it does not register the next PDN to arrive.
inline void fdnIgnoresTapWithNothingPending(FdnProvisioningTests* suite) {
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->tapBand(fdnProv);
    EXPECT_TRUE(suite->queuedRequests.empty());

    suite->cablePdn(SerialIdentifier::INPUT_JACK, suite->pdnMac);
    fdnProv.sync();
    fdnProv.sync();
    ASSERT_TRUE(fdnProv.hasPendingRegistration());
    EXPECT_TRUE(suite->queuedRequests.empty()) << "a tap from before the PDN arrived was kept";

    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 1u);
}

inline void fdnIgnoresTapDuringALookup(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);

    suite->tapBand(fdnProv, {0x04, 0x11, 0x22, 0x33});
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "a second lookup started under the first";

    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_TRUE(suite->wroteTestKeyTo(SerialIdentifier::INPUT_JACK));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(suite->results, (std::vector<std::string>{"Ada"}));
}

// The tap is for whichever PDN holds the pending jack when it lands, not the
// one that held it when the registration was queued.
inline void fdnBindsTapToThePdnOnThePendingJackAtTapTime(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->captureRadio();
    fdnProv.sync();
    ASSERT_TRUE(fdnProv.hasPendingRegistration());

    suite->fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, suite->secondPdnMac);
    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    ASSERT_EQ(suite->replies.size(), 1u);
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
    EXPECT_TRUE(suite->replyWentTo(0, suite->secondPdnMac))
    << "the profile went to the PDN that had left the jack";
}

// The PDN bound at the tap is swapped for another on the same jack during the
// lookup: the key was looked up for neither, so none is written and nothing is sent.
inline void
fdnWritesNoKeyWhenThePendingJacksPdnIsSwappedDuringTheLookup(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);

    suite->cablePdn(SerialIdentifier::INPUT_JACK, suite->secondPdnMac);
    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_TRUE(suite->fdnRdc.getSentApplicationFrames().empty())
    << "the key was written for a PDN the band was not tapped for";
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
    EXPECT_TRUE(suite->replies.empty())
    << "a profile or failure went to a PDN that was swapped out";
}

// With two PDNs cabled, the key is for the pending jack's PDN: it goes to that
// jack, and only that jack's report releases the profile, sent to that PDN.
inline void fdnWritesKeyOnlyToThePendingJack(FdnProvisioningTests* suite) {
    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->captureRadio();
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    fdnProv.sync();
    suite->cablePdn(SerialIdentifier::INPUT_JACK, suite->pdnMac);

    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    ASSERT_EQ(suite->fdnRdc.getSentApplicationFrames().size(), 1u);
    EXPECT_TRUE(suite->wroteTestKeyTo(SerialIdentifier::INPUT_JACK_SECONDARY))
    << "the key went to a jack whose registration was not pending";

    // Some other frame finishes on the primary jack.
    ApplicationPayload unrelated{};
    ASSERT_TRUE(suite->fdnRdc.sendApplicationFrame(SerialIdentifier::INPUT_JACK, unrelated));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    EXPECT_TRUE(suite->replies.empty()) << "another jack's report released the profile";
    EXPECT_TRUE(fdnProv.isLookupInFlight());

    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK_SECONDARY);
    ASSERT_EQ(suite->replies.size(), 1u);
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
    EXPECT_TRUE(suite->replyWentTo(0, suite->secondPdnMac));
}

inline void fdnLooksUpOnlyFourAndSevenByteUids(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->tapBand(fdnProv, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A});
    EXPECT_TRUE(suite->queuedRequests.empty());
    EXPECT_TRUE(fdnProv.hasPendingRegistration()) << "a bad read ended the registration";
    EXPECT_FALSE(fdnProv.isLookupInFlight());

    suite->tapBand(fdnProv, {0x04, 0x5E, 0x21, 0x7A, 0x3B, 0x6C, 0x80});
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_EQ(suite->queuedRequests[0].path, "/device/players/by-band/045E217A3B6C80");
}

// During symbol match nothing is pending: no timer runs and a tap issues no
// lookup until the registration screen is shown.
inline void fdnHoldsTheQueueWhileTheRegistrationScreenIsNotShown(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    suite->tapBand(fdnProv);
    EXPECT_FALSE(fdnProv.hasPendingRegistration());
    EXPECT_TRUE(suite->queuedRequests.empty()) << "no Wi-Fi excursion during symbol match";

    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 1);
    fdnProv.sync();
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    ASSERT_TRUE(fdnProv.hasPendingRegistration())
    << "the queue was lost off the registration screen";
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK)
    << "the pending timer ran off the registration screen";

    // Leaving the screen stops the timer; back on it, the turn starts in full.
    suite->fakeClock->advance(1000);
    fdnProv.setRegistrationScreenShown(false);
    fdnProv.sync();
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK)
    << "time off the registration screen counted against the turn";

    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "the registration screen takes the tap";
}

// The prompt named one jack a moment ago, so a tap just after the turn passes to
// the other jack is not taken for that jack's PDN.
inline void fdnIgnoresTapJustAfterThePendingJackChanges(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    fdnProv.sync();
    ASSERT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK);

    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 1);
    fdnProv.sync();
    ASSERT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK_SECONDARY);
    suite->fakeClock->advance(1000);
    suite->tapBand(fdnProv);
    EXPECT_TRUE(suite->queuedRequests.empty())
    << "a tap 1000 ms after the turn moved was looked up";
    ASSERT_TRUE(fdnProv.hasPendingRegistration());
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK_SECONDARY);

    suite->fakeClock->advance(FdnProvisioningManager::PENDING_JACK_CHANGE_TAP_IGNORE_MS - 1000 + 1);
    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_TRUE(suite->wroteTestKeyTo(SerialIdentifier::INPUT_JACK_SECONDARY));

    // A lookup that ends with the other jack next moves the turn as well, once its
    // result has had its time on the screen.
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK_SECONDARY);
    suite->waitOutLookupResult(fdnProv);
    ASSERT_TRUE(fdnProv.hasPendingRegistration());
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK);
    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 1u)
    << "a tap as the turn passed on after a lookup was looked up";
}

// A finished lookup's result holds the next registration back for its screen
// time: the PDN shows its own failure page meanwhile and refuses a key.
inline void fdnIgnoresTapWhileALookupResultIsHeld(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    EXPECT_TRUE(fdnProv.isHoldingLookupResult());

    suite->fakeClock->advance(FdnProvisioningManager::LOOKUP_RESULT_HOLD_MS);
    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 1u) << "a tap during the result screen was looked up";
    EXPECT_FALSE(fdnProv.hasPendingRegistration());

    suite->fakeClock->advance(1);
    fdnProv.sync();
    EXPECT_FALSE(fdnProv.isHoldingLookupResult());
    ASSERT_TRUE(fdnProv.hasPendingRegistration())
    << "the failed PDN was not requeued after its result";
    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 2u);
}

// An old PDN's PIN request (type 26) must not be read as anything.
inline void fdnIgnoresTheRetiredRequestType(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    const uint8_t oldRequest[6] = {1, '1', '2', '3', '4', 0};
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(suite->pdnMac, static_cast<PktType>(26),
                                                          oldRequest, sizeof(oldRequest));
    fdnProv.sync();
    EXPECT_TRUE(suite->queuedRequests.empty());
    EXPECT_FALSE(fdnProv.isLookupInFlight());
}

// ---- The reader ----

inline void fdnWithoutAReaderStillCyclesTheQueue(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc, nullptr);
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    EXPECT_FALSE(fdnProv.isBandReaderResponding());
    ASSERT_TRUE(fdnProv.hasPendingRegistration());

    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 1);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK_SECONDARY)
    << "a pending registration with no reader never timed out";
    EXPECT_TRUE(suite->queuedRequests.empty());
}

inline void fdnAcceptsTapsOnceTheReaderAnswersAgain(FdnProvisioningTests* suite) {
    suite->reader.setResponding(false);
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->tapBand(fdnProv);
    EXPECT_FALSE(fdnProv.isBandReaderResponding());
    EXPECT_TRUE(suite->queuedRequests.empty());

    suite->reader.setResponding(true);
    EXPECT_TRUE(fdnProv.isBandReaderResponding());
    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 1u);
}

// ---- Lookup and delivery ----

inline void fdnCancelledLookupLeavesWifiAtOnce(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::WIFI);

    fdnProv.cancelLookup();
    EXPECT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW)
    << "the FDN stayed deaf to ESP-NOW for a PDN that is no longer there";

    // Leaving Wi-Fi fails the queued request, as the HTTP driver's disconnect does.
    suite->queuedRequests[0].onError({WirelessError::CONNECTION_FAILED, "Disconnected"});
    suite->waitOutLookupResult(fdnProv);
    suite->tapBand(fdnProv);
    EXPECT_EQ(suite->queuedRequests.size(), 2u) << "the cancelled lookup blocks the next one";
}

inline void fdnReportsLookupResultAndResendsUnconfirmedReply(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    int replyFrames = 0;
    ON_CALL(*suite->fdnDevice.mockPeerComms,
            sendData(testing::_, PktType::kPlayerProvision, testing::_, testing::_))
    .WillByDefault([&replyFrames](const uint8_t*, PktType, const uint8_t*, const size_t) {
        replyFrames++;
        return 1;
    });

    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    EXPECT_TRUE(fdnProv.isLookupInFlight());

    suite->queuedRequests[0].onSuccess(encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(suite->results, (std::vector<std::string>{"Ada"}));
    EXPECT_EQ(suite->resultJacks, (std::vector<SerialIdentifier>{SerialIdentifier::INPUT_JACK}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());
    EXPECT_EQ(replyFrames, 1);

    // No SEND_SUCCESS came back, so the reply goes out again on the backoff.
    suite->fakeClock->advance(150);
    suite->fdnDevice.wirelessManager->sync();
    EXPECT_EQ(replyFrames, 2) << "a reply the radio did not confirm is resent";
}

// The key crosses the jack and only the jack; the profile waits for the jack's word.
inline void fdnWritesKeyToJackThenSendsProfile(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();

    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    ASSERT_EQ(suite->fdnRdc.getSentApplicationFrames().size(), 1u);
    EXPECT_TRUE(suite->wroteTestKeyTo(SerialIdentifier::INPUT_JACK));

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
    EXPECT_TRUE(suite->replyWentTo(0, suite->pdnMac));
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

// Each way a lookup can fail reaches the registration screen as its own cause,
// and reaches the PDN as the one lookup-failure reply with no key before it.
inline void fdnReportsEachLookupFailureByItsCause(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    const std::vector<WirelessErrorInfo> errors = {
    {WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404},
    {WirelessError::INVALID_RESPONSE, "HTTP Error: 400", 400},
    {WirelessError::SERVER_ERROR, "HTTP Error: 503", 503},
    {WirelessError::INVALID_RESPONSE, "HTTP Error: 403", 403},
    {WirelessError::TIMEOUT, "Request timed out"},
    {WirelessError::CONNECTION_FAILED, "ESP_ERR_HTTP_CONNECT"},
    {WirelessError::WIFI_NOT_CONNECTED, "WiFi lost"},
    {WirelessError::SERVER_NOT_TRUSTED, "Server key not pinned"},
    };
    for (const WirelessErrorInfo& error : errors) {
        suite->waitOutLookupResult(fdnProv);
        suite->tapBand(fdnProv);
        ASSERT_FALSE(suite->queuedRequests.empty());
        suite->queuedRequests.back().onError(error);
    }
    // No answer at all within the lookup's time limit.
    suite->waitOutLookupResult(fdnProv);
    suite->tapBand(fdnProv);
    suite->fakeClock->advance(PROVISION_LOOKUP_TIMEOUT_MS + 1);
    fdnProv.sync();

    EXPECT_EQ(suite->queuedRequests.size(), errors.size() + 1)
    << "a failed PDN was not requeued for another tap";
    EXPECT_EQ(suite->results, (std::vector<std::string>{
                              "<not found>", "<not found>", "<failed>", "<failed>", "<unreachable>",
                              "<unreachable>", "<unreachable>", "<not trusted>", "<unreachable>"}));
    ASSERT_EQ(suite->replies.size(), errors.size() + 1);
    for (size_t i = 0; i < suite->replies.size(); ++i) {
        EXPECT_EQ(suite->replies[i].status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED))
        << "reply " << i;
        EXPECT_STREQ(suite->replies[i].playerName, "") << "reply " << i;
        EXPECT_TRUE(suite->replyWentTo(i, suite->pdnMac)) << "reply " << i;
    }
    EXPECT_TRUE(suite->fdnRdc.getSentApplicationFrames().empty()) << "a key reached the jack";
    EXPECT_EQ(suite->fdnDevice.wirelessManager->getCurrentMode(), WirelessMode::ESPNOW)
    << "Wi-Fi was kept after the lookup ended";
}

// After a failure the PDN, still cabled, waits behind the one queued after it.
inline void fdnRequeuesAFailedRegistrationAtTheBack(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    fdnProv.sync();
    suite->cablePdn(SerialIdentifier::INPUT_JACK_SECONDARY, suite->secondPdnMac);
    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->queuedRequests.size(), 1u);
    suite->queuedRequests[0].onError({WirelessError::INVALID_RESPONSE, "HTTP Error: 404", 404});
    suite->waitOutLookupResult(fdnProv);
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK_SECONDARY);

    suite->fakeClock->advance(FdnProvisioningManager::REGISTRATION_PENDING_MS + 1);
    fdnProv.sync();
    EXPECT_EQ(fdnProv.getPendingJack(), SerialIdentifier::INPUT_JACK)
    << "the failed PDN was dropped instead of requeued";
}

// The server knows the player but issues no key the FDN accepts: the PDN is
// told the lookup failed and the listener hears that no key was issued.
inline void fdnLookupWithoutUsableKeyFailsRegistration(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    const std::string id = uuidWireBytes(TEST_PLAYER_ID);
    std::string shortKey = encodeTestPlayerWithoutKey(id, "Ada");
    appendTestRecordKey(shortKey, testRecordKeyWire(RECORD_KEY_SIZE - 1));
    std::string longKey = encodeTestPlayerWithoutKey(id, "Ada");
    appendTestRecordKey(longKey, testRecordKeyWire(RECORD_KEY_SIZE + 1));

    size_t lookups = 0;
    for (const std::string& body : {encodeTestPlayerWithoutKey(id, "Ada"), shortKey, longKey}) {
        suite->waitOutLookupResult(fdnProv);
        suite->answerLookup(fdnProv, body);
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
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    ON_CALL(*suite->fdnDevice.mockHttpClient, isResponseServerVerified())
    .WillByDefault(testing::Return(false));

    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    EXPECT_TRUE(suite->fdnRdc.getSentApplicationFrames().empty())
    << "a key from an unverified server reached the jack";
    ASSERT_EQ(suite->replies.size(), 1u);
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
}

inline void fdnJackRefusingKeyFrameFailsRegistration(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    suite->fdnRdc.setApplicationFramesRefused(true);

    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    ASSERT_EQ(suite->replies.size(), 1u) << "a still-Connected PDN was not told";
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());

    // The refusal is not remembered against the next attempt.
    suite->fdnRdc.setApplicationFramesRefused(false);
    suite->waitOutLookupResult(fdnProv);
    suite->answerLookup(fdnProv, encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada"));
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    ASSERT_EQ(suite->replies.size(), 2u);
    EXPECT_EQ(suite->replies[1].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
}

// A PDN that is gone gets neither a profile nor a failure notice, at whichever
// point of the delivery it left.
inline void fdnSendsNothingToPdnThatLeftBeforeKeyWasWritten(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    const std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");
    FakeRemoteDeviceCoordinator& rdc = suite->fdnRdc;

    // Left while the frame waited for the writer: the jack reports it dropped.
    suite->answerLookup(fdnProv, body);
    ASSERT_EQ(rdc.getSentApplicationFrames().size(), 1u);
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
    EXPECT_FALSE(fdnProv.isLookupInFlight());

    // Left while the server was still answering: the key never reaches the jack.
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    suite->waitOutLookupResult(fdnProv);
    suite->tapBand(fdnProv);
    ASSERT_EQ(suite->queuedRequests.size(), 2u);
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::DISCONNECTED);
    suite->queuedRequests[1].onSuccess(body);
    EXPECT_EQ(rdc.getSentApplicationFrames().size(), 1u) << "a key was written for a PDN that left";
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>", "<no key>"}));

    // Left mid-write: the jack reports the frame written, to a PDN that is gone.
    rdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    suite->waitOutLookupResult(fdnProv);
    suite->answerLookup(fdnProv, body);
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
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();
    const std::string body = encodeTestPlayer(uuidWireBytes(TEST_PLAYER_ID), "Ada");

    suite->answerLookup(fdnProv, body);
    ASSERT_TRUE(fdnProv.isLookupInFlight());
    fdnProv.cancelLookup();
    EXPECT_FALSE(fdnProv.isLookupInFlight());
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));
    ASSERT_EQ(suite->replies.size(), 1u) << "a still-Connected PDN was not told";
    EXPECT_EQ(suite->replies[0].status, static_cast<uint8_t>(ProvisionStatus::LOOKUP_FAILED));

    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    EXPECT_EQ(suite->replies.size(), 1u) << "the cancelled lookup's profile followed its key";
    EXPECT_EQ(suite->results, (std::vector<std::string>{"<no key>"}));

    suite->waitOutLookupResult(fdnProv);
    suite->answerLookup(fdnProv, body);
    suite->fdnRdc.finishApplicationFrameWrite(SerialIdentifier::INPUT_JACK);
    ASSERT_EQ(suite->replies.size(), 2u) << "the cancelled lookup blocks the next one";
    EXPECT_EQ(suite->replies[1].status, static_cast<uint8_t>(ProvisionStatus::PROFILE_DELIVERED));
}

// On a device giving Wi-Fi up fails the request; an answer that arrives anyway
// belongs to a lookup that is over.
inline void fdnAnswerToCancelledLookupWritesNoKey(FdnProvisioningTests* suite) {
    FdnProvisioningManager fdnProv(suite->fdnDevice.wirelessManager, &suite->fdnRdc,
                                   &suite->reader);
    fdnProv.setRegistrationScreenShown(true);
    suite->listenForResults(fdnProv);
    suite->captureRadio();

    suite->tapBand(fdnProv);
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
