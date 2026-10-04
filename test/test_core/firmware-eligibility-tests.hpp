#pragma once

#include <gtest/gtest.h>

#include "firmware-receiver-tests.hpp"
#include "firmware-seed-tests.hpp"
#include "device/firmware-update-manager.hpp"
#include "device/firmware-verify.hpp"

#include <cstdint>

TEST(FirmwareEligibilityTest, aDeviceInAMatchDoesNotAnswer) {
    FirmwareReceiverFixture f;
    f.setEligible(false);
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    EXPECT_FALSE(f.manager->isReceiving());
}

TEST(FirmwareEligibilityTest, aDeviceWithACableDoesNotAnswer) {
    FirmwareReceiverFixture f;
    f.setCableConnected(true);
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    EXPECT_FALSE(f.manager->isReceiving());
}

TEST(FirmwareEligibilityTest, noWireCommandCanStartASeed) {
    // Sweeps every possible command byte through the receive entry point:
    // beginSeeding is the only place that ever sets seeding=true, and it is
    // reachable only from the operator's button hold, never from the radio.
    FirmwareSeedFixture f;
    uint8_t frame[sizeof(FirmwareOfferPayload)] = {};
    for (int command = 0; command <= 0xFF; ++command) {
        frame[0] = static_cast<uint8_t>(command);
        f.manager->onPacketReceived(SEED_MAC, frame, sizeof(frame));
        ASSERT_FALSE(f.manager->isSeeding()) << "command byte " << command << " started a seed";
    }
}

TEST(FirmwareEligibilityTest, anOfferForAnotherDeviceTypeIsRefused) {
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOfferForDeviceType(DeviceType::FDN, 5000, 4));
    EXPECT_FALSE(f.manager->isReceiving());
}

TEST(FirmwareEligibilityTest, anOfferForThisDeviceTypeIsAccepted) {
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOfferForDeviceType(DeviceType::PDN, 5000, 4));
    EXPECT_TRUE(f.manager->isReceiving());
}

TEST(FirmwareEligibilityTest, deviceTypeRefusalIsNotASignatureFailure) {
    // Same offer verifyOffer alone accepts (proving it is genuinely and
    // correctly signed) is still refused by onOffer: the type check is doing
    // the work, not an incidental signature mismatch.
    FirmwareReceiverFixture f;
    const FirmwareOfferPayload offer = f.signedOfferForDeviceType(DeviceType::FDN, 5000, 4);
    ASSERT_EQ(verifyOffer(offer, TEST_ROOT_PUBLIC_KEY, /*minGeneration=*/1), FirmwareResult::OK);
    f.manager->onOffer(SEED_MAC, offer);
    EXPECT_FALSE(f.manager->isReceiving());
}

TEST(FirmwareEligibilityTest, ineligibleMidTransferAbortsAndNeverCommits) {
    // onOffer only gates the accept; a match starting partway through a
    // transfer must stop the receive rather than let it run to a
    // commit-and-restart on a device now in use.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    ASSERT_TRUE(f.manager->isReceiving());

    uint8_t body[FirmwareReceiverFixture::CHUNK_SIZE] = {0};
    f.deliverChunk(0, body, sizeof(body));
    ASSERT_EQ(f.manager->receivedCount(), 1);

    f.setEligible(false);
    f.manager->sync();
    EXPECT_FALSE(f.manager->isReceiving());

    // The transfer is closed, not paused: further chunks land nowhere, and
    // the commit path — which would set the boot target — never runs.
    f.deliverChunk(1, body, sizeof(body));
    f.deliverChunk(2, body, sizeof(body));
    f.deliverChunk(3, body, sizeof(body));
    EXPECT_EQ(f.manager->receivedCount(), 1);
    EXPECT_FALSE(f.store.bootSet());
}
