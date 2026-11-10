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

    FakePlatformClock* fakeClock = nullptr;
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    uint8_t fdnMac[6] = {0xFD, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    std::vector<HttpRequest> queuedRequests;
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
    std::vector<std::string> results;
    fdnProv.setOnLookupDone(
    [&results](const char* name) { results.push_back(name != nullptr ? name : "<failed>"); });
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
