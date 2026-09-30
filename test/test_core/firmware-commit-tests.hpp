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

TEST(FirmwareCommitTest, anOfferIsRefusedWhileACompletionReportIsStillQueued) {
    // A failed commit leaves receiving false and restartPending unset, so
    // nothing else stops an offer arriving before sync() flushes the report:
    // it would overwrite the hash sendComplete has yet to read, and the
    // failure would go out named after an image that never failed.
    FirmwareReceiverFixture f;
    f.receiveCompleteImageWithOneCorruptChunk(/*flushComplete=*/false);
    ASSERT_EQ(f.comms.countOf(FirmwareCmd::COMPLETE), 0);

    f.manager->onOffer(SEED_MAC, f.signedOfferForImage(f.imageBytes()));

    EXPECT_FALSE(f.manager->isReceiving());
    EXPECT_EQ(f.store.beginWriteCalls(), 1) << "an offer must not reopen the slot behind a pending report";

    f.manager->sync();
    ASSERT_EQ(f.comms.countOf(FirmwareCmd::COMPLETE), 1);
    EXPECT_EQ(f.lastResult(), FirmwareResult::BAD_HASH);

    const std::vector<uint8_t> failedImage = f.commitImageBytes();
    uint8_t failedHash[FIRMWARE_SHA256_LENGTH];
    sha256(failedImage.data(), failedImage.size(), failedHash);
    EXPECT_EQ(std::memcmp(f.comms.lastCompleteImageHash(), failedHash, FIRMWARE_SHA256_LENGTH), 0)
        << "COMPLETE must name the image that actually failed";
}

TEST(FirmwareCommitTest, nothingOnTheWireDisturbsAPendingReport) {
    // Row A's bad state — collecting a new image while a report on the last
    // one is still queued — is no longer a combination the manager can hold:
    // one phase member cannot be RECEIVING and REPORTING at once, so every
    // frame the wire can deliver during REPORTING is refused by the same
    // single check. Asserted from outside, since unrepresentability itself is
    // a property of the type: neither an offer, nor a chunk, nor a poll
    // changes what the report says.
    FirmwareReceiverFixture f;
    f.receiveCompleteImageWithOneCorruptChunk(/*flushComplete=*/false);

    f.manager->onOffer(SEED_MAC, f.signedOfferForImage(f.imageBytes()));
    uint8_t body[FirmwareReceiverFixture::CHUNK_SIZE] = {0};
    f.deliverChunk(0, body, sizeof(body));
    f.manager->onPoll(f.pollForCurrentImage());
    EXPECT_FALSE(f.manager->isReceiving());
    EXPECT_EQ(f.store.beginWriteCalls(), 1);

    f.manager->sync();
    ASSERT_EQ(f.comms.countOf(FirmwareCmd::COMPLETE), 1);
    EXPECT_EQ(f.comms.countOf(FirmwareCmd::STATUS), 0);
    EXPECT_EQ(f.lastResult(), FirmwareResult::BAD_HASH);

    const std::vector<uint8_t> failedImage = f.commitImageBytes();
    uint8_t failedHash[FIRMWARE_SHA256_LENGTH];
    sha256(failedImage.data(), failedImage.size(), failedHash);
    EXPECT_EQ(std::memcmp(f.comms.lastCompleteImageHash(), failedHash, FIRMWARE_SHA256_LENGTH), 0)
        << "COMPLETE must name the image that actually failed";
}

TEST(FirmwareCommitTest, committedDeviceRestarts) {
    // The spec requires a restart after setting the boot partition, or the
    // confirm timer armed at the next boot never arms anything.
    FirmwareReceiverFixture f;
    f.receiveCompleteValidImage();
    ASSERT_FALSE(f.store.didRestart());

    f.manager->sync();

    EXPECT_TRUE(f.store.didRestart());
}

TEST(FirmwareCommitTest, commitRaisesTheStoredGenerationFloor) {
    // Revocation is by generation: taking an image signed under a higher one
    // is what retires the certificates below it. Without this the floor stays
    // at its NVS default for the life of the device and a leaked signer key
    // never expires.
    FirmwareReceiverFixture f;
    ASSERT_EQ(f.store.getMinGeneration(), 0);

    f.receiveCompleteValidImage();  // the fixture signs its offers at generation 1

    ASSERT_TRUE(f.store.bootSet());
    EXPECT_EQ(f.store.getMinGeneration(), 1);
}

TEST(FirmwareCommitTest, anOfferBelowTheStoredFloorIsRefused) {
    // The other half of revocation: once the floor has risen, a certificate
    // under it buys nothing, so the leaked signer's offers open no slot.
    FirmwareReceiverFixture f;
    f.store.setMinGeneration(2);

    f.manager->onOffer(SEED_MAC, f.signedOffer(/*length=*/5000, /*chunks=*/4));

    EXPECT_FALSE(f.manager->isReceiving());
    EXPECT_EQ(f.store.beginWriteCalls(), 0);
}

TEST(FirmwareCommitTest, theRestartProceedsWhenTheReportCannotBeSent) {
    // COMPLETE is sequenced before the restart, but nothing in the fleet
    // decodes it: a radio that keeps refusing the frame used to leave the
    // device running the old image with its boot partition already switched.
    FirmwareReceiverFixture f;
    f.comms.refuseEverySend();
    f.receiveCompleteValidImage();
    ASSERT_TRUE(f.store.bootSet());
    ASSERT_FALSE(f.store.didRestart());

    for (int tick = 0; tick < 10; tick++) {
        f.manager->sync();
    }

    EXPECT_TRUE(f.store.didRestart());
    EXPECT_EQ(f.comms.countOf(FirmwareCmd::COMPLETE), 0);
}
