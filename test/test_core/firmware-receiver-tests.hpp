#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "fake-firmware-store.hpp"
#include "firmware-test-keys.hpp"
#include "device/firmware-update-manager.hpp"
#include "device/firmware-verify.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using ::testing::NiceMock;

/// A MAC standing in for the seed device throughout the firmware-distribution
/// suites.
inline constexpr uint8_t SEED_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

/// Shared by every firmware-distribution suite: a receiver manager wired to a
/// fake flash slot and a mock radio, plus builders for signed offers and
/// chunk delivery. Tasks 9-12 include this header rather than redefine it,
/// so every accessor a later task's tests call must be declared here.
class FirmwareReceiverFixture {
public:
    /// Wires a fresh manager to `comms`/`store` and gives the slot enough
    /// room for an ordinary test image, so most tests need not configure it.
    FirmwareReceiverFixture()
        : manager(new FirmwareUpdateManager(&comms, &store, TEST_ROOT_PUBLIC_KEY)) {
        store.setInactiveSlotSize(4 * 1024 * 1024);
    }

    /// Frees the manager this fixture owns.
    ~FirmwareReceiverFixture() { delete manager; }

    /// A signed offer for a synthetic image of `length` bytes split into
    /// `chunks` chunks of CHUNK_SIZE bytes each (the last one short).
    FirmwareOfferPayload signedOffer(uint32_t length, uint16_t chunks) {
        FirmwareOfferPayload offer = firmware_test_keys::buildOffer(/*generation=*/1, TEST_IMAGE_SHA);
        offer.imageLength = length;
        offer.chunkSize = CHUNK_SIZE;
        offer.chunkCount = chunks;
        resign(offer);
        return offer;
    }

    /// Delivers one chunk directly to the manager, as if the radio had just
    /// handed it a CHUNK frame's body.
    void deliverChunk(uint16_t index, const uint8_t* data, uint16_t length) {
        FirmwareChunkHeader header{};
        header.command = static_cast<uint8_t>(FirmwareCmd::CHUNK);
        header.index = index;
        header.length = length;
        manager->onChunk(header, data);
    }

    /// Bytes of a synthetic "running image", small enough to hash quickly.
    std::vector<uint8_t> imageBytes() const {
        std::vector<uint8_t> bytes(256);
        for (size_t i = 0; i < bytes.size(); i++) {
            bytes[i] = static_cast<uint8_t>(i);
        }
        return bytes;
    }

    /// A signed offer whose hash matches imageBytes(), for the
    /// already-running-this-image case.
    FirmwareOfferPayload signedOfferForRunningImage() {
        const std::vector<uint8_t> bytes = imageBytes();
        uint8_t hash[FIRMWARE_SHA256_LENGTH];
        sha256(bytes.data(), bytes.size(), hash);
        FirmwareOfferPayload offer = firmware_test_keys::buildOffer(/*generation=*/1, hash);
        offer.imageLength = static_cast<uint32_t>(bytes.size());
        offer.chunkSize = CHUNK_SIZE;
        offer.chunkCount = 1;
        resign(offer);
        return offer;
    }

    NiceMock<MockPeerComms> comms;
    FakeFirmwareStore store;
    FirmwareUpdateManager* manager;

    /// Chunk body size used by every offer this fixture builds.
    static constexpr uint16_t CHUNK_SIZE = 1400;

private:
    // Re-signs the image span after mutating length/chunkSize/chunkCount:
    // those fields sit inside the span buildOffer already signed.
    void resign(FirmwareOfferPayload& offer) {
        const uint8_t* signedStart =
            reinterpret_cast<const uint8_t*>(&offer) + offsetof(FirmwareOfferPayload, imageSha256);
        const size_t signedLength =
            offsetof(FirmwareOfferPayload, cert) - offsetof(FirmwareOfferPayload, imageSha256);
        firmware_test_keys::signRaw(firmware_test_keys::signerKeypair().privateKey, signedStart,
                                    signedLength, offer.imageSignature);
    }
};

TEST(FirmwareReceiverTest, offerOpensTheSlotForTheDeclaredLength) {
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(/*length=*/5000, /*chunks=*/4));
    EXPECT_TRUE(f.manager->isReceiving());
    EXPECT_EQ(f.store.beginWriteLength(), 5000u);
}

TEST(FirmwareReceiverTest, chunkLandsAtItsIndexedOffset) {
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t body[1400];
    memset(body, 0xAB, sizeof(body));
    f.deliverChunk(/*index=*/2, body, sizeof(body));
    EXPECT_EQ(f.store.slot()[2 * 1400], 0xAB);
    EXPECT_EQ(f.manager->receivedCount(), 1);
}

TEST(FirmwareReceiverTest, chunkBeyondTheDeclaredCountIsDropped) {
    // A corrupt or hostile index must not write outside the image.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t body[1400] = {0};
    f.deliverChunk(/*index=*/4, body, sizeof(body));  // valid indices are 0..3
    EXPECT_EQ(f.manager->receivedCount(), 0);
    EXPECT_FALSE(f.store.wroteOutsideImage());
}

TEST(FirmwareReceiverTest, anOfferMatchingTheRunningImageIsIgnored) {
    // Identity is the hash, so a device already running this image must not
    // erase its spare slot to receive what it already has.
    FirmwareReceiverFixture f;
    f.store.setRunningImage(f.imageBytes());
    f.manager->onOffer(SEED_MAC, f.signedOfferForRunningImage());
    EXPECT_FALSE(f.manager->isReceiving());
    EXPECT_EQ(f.store.beginWriteCalls(), 0);
}

TEST(FirmwareReceiverTest, anOfferExceedingTheBitmapIsIgnored) {
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(/*length=*/9'000'000, /*chunks=*/4000));
    EXPECT_FALSE(f.manager->isReceiving());
}

TEST(FirmwareReceiverTest, duplicateChunkIsNotCountedTwice) {
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t body[1400] = {0};
    f.deliverChunk(1, body, sizeof(body));
    f.deliverChunk(1, body, sizeof(body));
    EXPECT_EQ(f.manager->receivedCount(), 1);
}

TEST(FirmwareReceiverTest, receiverRefusesAnImageLargerThanItsSlot) {
    FirmwareReceiverFixture f;
    f.store.setInactiveSlotSize(4000);
    f.manager->onOffer(SEED_MAC, f.signedOffer(/*length=*/5000, /*chunks=*/4));
    EXPECT_FALSE(f.manager->isReceiving());
    EXPECT_EQ(f.store.beginWriteCalls(), 0);
}

TEST(FirmwareReceiverTest, repeatingTheSameOfferDoesNotResetProgress) {
    // The seed re-broadcasts OFFER once a second for the whole run; hearing
    // it again must not reopen the slot and wipe chunks already received.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t body[1400] = {0};
    f.deliverChunk(0, body, sizeof(body));
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    EXPECT_EQ(f.manager->receivedCount(), 1);
}
