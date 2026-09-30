#pragma once

#include <gtest/gtest.h>

#include "firmware-receiver-tests.hpp"
#include "device/firmware-update-manager.hpp"

#include <cstring>
#include <vector>

TEST(FirmwareCommitTest, bootIsSetOnlyAfterHashAndSignaturePass) {
    FirmwareReceiverFixture f;
    f.receiveCompleteValidImage();
    EXPECT_TRUE(f.store.bootSet());
    EXPECT_EQ(f.lastResult(), FirmwareResult::OK);
}

TEST(FirmwareCommitTest, corruptAssemblyLeavesBootAlone) {
    FirmwareReceiverFixture f;
    f.receiveCompleteImageWithOneCorruptChunk();
    EXPECT_FALSE(f.store.bootSet());
    EXPECT_EQ(f.lastResult(), FirmwareResult::BAD_HASH);
}

TEST(FirmwareCommitTest, aFailedFlashWriteNeverBoots) {
    // Power loss and a failing write land in the same place: the slot holds a
    // partial image and must never become the boot target.
    FirmwareReceiverFixture f;
    f.store.failWritesFrom(3000);
    f.receiveCompleteValidImage();
    EXPECT_FALSE(f.store.bootSet());
    EXPECT_EQ(f.lastResult(), FirmwareResult::FLASH_FAILED);
}

TEST(FirmwareCommitTest, commitWritesTheTrailerPastTheImage) {
    // The transfer never carries the trailer — the cert and signature ride
    // in the OFFER instead — so committing has to write one, or this device
    // could never seed the image onward: beginSeeding() reads it from
    // exactly this offset.
    FirmwareReceiverFixture f;
    f.receiveCompleteValidImage();
    ASSERT_TRUE(f.store.bootSet());

    const std::vector<uint8_t> image = f.commitImageBytes();
    ASSERT_GE(f.store.slot().size(), image.size() + sizeof(FirmwareTrailer));
    FirmwareTrailer trailer{};
    std::memcpy(&trailer, f.store.slot().data() + image.size(), sizeof(FirmwareTrailer));
    EXPECT_EQ(trailer.magic, FIRMWARE_TRAILER_MAGIC);
    EXPECT_EQ(trailer.imageLength, image.size());
}

TEST(FirmwareCommitTest, committedDeviceIgnoresARepeatOfferForTheSameImage) {
    // The seed rebroadcasts OFFER every second for the life of the run; a
    // committed-but-not-yet-restarted device must not let one reopen the
    // slot it just verified and pointed the boot selector at.
    FirmwareReceiverFixture f;
    f.receiveCompleteValidImage();
    ASSERT_EQ(f.store.beginWriteCalls(), 1);

    f.manager->onOffer(SEED_MAC, f.signedOfferForImage(f.commitImageBytes()));

    EXPECT_EQ(f.store.beginWriteCalls(), 1);
    EXPECT_TRUE(f.store.bootSet());
}

TEST(FirmwareCommitTest, committedDeviceRestarts) {
    // The spec requires a restart after setting the boot partition, or the
    // confirm timer armed at the next boot never arms anything.
    FirmwareReceiverFixture f;
    f.receiveCompleteValidImage();
    ASSERT_FALSE(f.store.didRestart());

    f.advance(600);
    f.manager->sync();

    EXPECT_TRUE(f.store.didRestart());
}
