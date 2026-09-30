#pragma once

#include <gtest/gtest.h>

#include "firmware-receiver-tests.hpp"
#include "firmware-seed-tests.hpp"
#include "device/firmware-update-manager.hpp"

#include <cstdint>
#include <set>

/// MACs standing in for devices reporting STATUS back to the seed. Distinct
/// from SEED_MAC (firmware-receiver-tests.hpp), which stands in for the
/// broadcasting seed itself.
inline constexpr uint8_t DEVICE_A[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
inline constexpr uint8_t DEVICE_B[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
inline constexpr uint8_t LATE_DEVICE[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x04};

TEST(FirmwareRepairTest, seedResendsOnlyTheUnionOfReportedGaps) {
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    f.streamAllChunks();
    f.deliverStatus(DEVICE_A, {4, 9});
    f.deliverStatus(DEVICE_B, {9, 12});
    f.runRepairRound();
    EXPECT_EQ(f.comms.chunkIndicesSent(), (std::set<uint16_t>{4, 9, 12}));
}

TEST(FirmwareRepairTest, statusForAnotherImageIsIgnored) {
    // A stale reply from a previous run must not pollute this one's repair set.
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    f.streamAllChunks();
    f.deliverStatusForImage(DEVICE_A, OTHER_IMAGE_SHA, {4});
    f.runRepairRound();
    EXPECT_TRUE(f.comms.chunkIndicesSent().empty());
}

TEST(FirmwareRepairTest, aDevicePoweredOnMidRunJoinsAtTheNextOffer) {
    // No cohort and no discovery window: this is the behaviour that buys.
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    f.streamAllChunks();
    FirmwareReceiverFixture late;
    late.manager->onOffer(SEED_MAC, f.currentOffer());
    EXPECT_TRUE(late.manager->isReceiving());
    f.deliverStatus(LATE_DEVICE, late.missingIndices());
    f.runRepairRound();
    EXPECT_EQ(f.comms.chunkIndicesSent(), late.missingIndices());
}

TEST(FirmwareRepairTest, runEndsWhenAPollFindsNothingMissing) {
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    f.streamAllChunks();
    f.runRepairRound();  // nobody reports a gap
    EXPECT_FALSE(f.manager->isSeeding());
}

TEST(FirmwareRepairTest, receiverAnswersAPollWithinTheBackoffWindow) {
    FirmwareReceiverFixture f;
    f.beginReceivingPartial();
    f.manager->onPoll(f.pollForCurrentImage());
    EXPECT_EQ(f.comms.countOf(FirmwareCmd::STATUS), 0) << "answered without backoff";
    f.advance(500);
    f.manager->sync();
    EXPECT_EQ(f.comms.countOf(FirmwareCmd::STATUS), 1);
}

TEST(FirmwareRepairTest, aRunEndsWhenRepairStopsMakingProgress) {
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    f.streamAllChunks();
    for (int round = 0; round < 3; ++round) {
        f.deliverStatus(DEVICE_A, {7});  // same gap every round
        f.runRepairRound();
    }
    EXPECT_FALSE(f.manager->isSeeding());
}

TEST(FirmwareRepairTest, receiversWithDifferentMacsDrawDifferentFirstDelays) {
    // Backoff state is per device (seeded from its own MAC), not the shared
    // global RNG: two receivers answering the same poll must not collide on
    // the same delay just because they're running identical firmware.
    // One fixture at a time: FirmwareReceiverFixture registers SimpleTimer's
    // one global clock in its constructor, so two alive together would have
    // the second one's construction hijack the first's clock out from under
    // it.
    const uint8_t macA[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x10};
    const uint8_t macB[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x20};
    unsigned long delayA = 0;
    {
        FirmwareReceiverFixture a(macA);
        a.beginReceivingPartial();
        a.manager->onPoll(a.pollForCurrentImage());
        delayA = a.delayUntilStatusSent();
    }
    unsigned long delayB = 0;
    {
        FirmwareReceiverFixture b(macB);
        b.beginReceivingPartial();
        b.manager->onPoll(b.pollForCurrentImage());
        delayB = b.delayUntilStatusSent();
    }
    EXPECT_NE(delayA, delayB);
}

TEST(FirmwareRepairTest, successiveBackoffDrawsFromTheSameReceiverDiffer) {
    // A fixed per-device delay (e.g. derived straight from the MAC with no
    // further state) would collide with itself every round forever.
    FirmwareReceiverFixture f;
    f.beginReceivingPartial();
    f.manager->onPoll(f.pollForCurrentImage());
    const unsigned long first = f.delayUntilStatusSent();
    f.manager->onPoll(f.pollForCurrentImage());
    const unsigned long second = f.delayUntilStatusSent();
    EXPECT_NE(first, second);
}
