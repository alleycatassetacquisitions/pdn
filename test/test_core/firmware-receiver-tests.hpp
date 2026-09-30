#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "fake-firmware-store.hpp"
#include "firmware-test-keys.hpp"
#include "device/firmware-update-manager.hpp"
#include "device/firmware-verify.hpp"
#include "device/drivers/platform-clock.hpp"
#include "utils/simple-timer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

/// A MAC standing in for the seed device throughout the firmware-distribution
/// suites.
inline constexpr uint8_t SEED_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

/// Clock this suite drives by hand so a receiver's POLL-reply backoff can be
/// fast-forwarded without a real wait. Duplicated from firmware-seed-tests.hpp's
/// FakeSeedClock rather than shared: each fixture file owns registering and
/// clearing SimpleTimer's global clock for its own lifetime.
class FakeReceiverClock : public PlatformClock {
public:
    /// Current simulated time in milliseconds.
    unsigned long milliseconds() override { return currentTime; }
    /// Moves the simulated clock forward by `delta` milliseconds.
    void advance(unsigned long delta) { currentTime += delta; }

private:
    unsigned long currentTime = 0;
};

/// Records every frame FirmwareUpdateManager sends, the same way
/// firmware-seed-tests.hpp's SeedComms does for the seed suite: real
/// sendData bookkeeping so a test can assert counts directly instead of an
/// EXPECT_CALL per send, layered on the gmock base this fixture already
/// relies on for setPacketHandler capture.
class ReceiverComms : public NiceMock<MockPeerComms> {
public:
    /// Stubs getGlobalBroadcastAddress so the destination sendData is given
    /// is a valid pointer rather than NiceMock's default nullptr.
    ReceiverComms() {
        ON_CALL(*this, getGlobalBroadcastAddress())
            .WillByDefault(Invoke([this]() -> const uint8_t* { return broadcastAddress; }));
    }

    /// Real behavior rather than a MOCK_METHOD: records every send so a
    /// test can assert counts without an EXPECT_CALL per frame.
    int sendData(const uint8_t*, PktType, const uint8_t* data, const size_t length) override {
        totalSent++;
        if (length > 0 && data[0] < commandCounts.size()) {
            commandCounts[data[0]]++;
        }
        return 0;
    }

    /// Count of every frame sent so far, of any command.
    int sentCount() const { return totalSent; }
    /// Count of frames sent so far whose leading command byte is `cmd`.
    int countOf(FirmwareCmd cmd) const { return commandCounts[static_cast<size_t>(cmd)]; }

private:
    int totalSent = 0;
    std::array<int, 5> commandCounts{};
    uint8_t broadcastAddress[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
};

/// Shared by every firmware-distribution suite: a receiver manager wired to a
/// fake flash slot and a mock radio, plus builders for signed offers and
/// chunk delivery. Included rather than redefined wherever a suite needs a
/// receiver, so every accessor a test calls must be declared here.
class FirmwareReceiverFixture {
public:
    /// Real OTA partition size (partitions.csv), the fixture's default slot.
    static constexpr size_t INACTIVE_SLOT_SIZE = 0x3E0000;
    /// Chunk body size used by every offer this fixture builds, unless a
    /// caller asks for a different one via signedOfferWithChunkSize.
    static constexpr uint16_t CHUNK_SIZE = 1400;

    /// Wires a fresh manager to `comms`/`store`, gives the slot its real
    /// size, and captures the radio callback the manager registers so
    /// deliverFrame can drive the actual wire-receive path.
    FirmwareReceiverFixture() {
        SimpleTimer::setPlatformClock(&clock);
        store.setInactiveSlotSize(INACTIVE_SLOT_SIZE);
        ON_CALL(comms, setPacketHandler(PktType::kFirmwareUpdate, _, _))
            .WillByDefault(Invoke([this](PktType, PeerCommsInterface::PacketCallback callback, void* ctx) {
                rawHandler = callback;
                rawCtx = ctx;
            }));
        manager = new FirmwareUpdateManager(&comms, &store, TEST_ROOT_PUBLIC_KEY);
    }

    /// Frees the manager this fixture owns and un-registers the fake clock
    /// so it does not outlive this fixture for a later test's SimpleTimer.
    ~FirmwareReceiverFixture() {
        delete manager;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /// Moves the fixture's fake clock forward by `ms` milliseconds, without
    /// calling sync() — callers drive sync() themselves so they control
    /// exactly when a deferred send is attempted.
    void advance(unsigned long ms) { clock.advance(ms); }

    /// Accepts a signed offer, then delivers one chunk — a receiver mid-run
    /// with more chunks still missing than landed.
    void beginReceivingPartial() {
        manager->onOffer(SEED_MAC, signedOffer(/*length=*/5000, /*chunks=*/4));
        uint8_t body[CHUNK_SIZE] = {0};
        deliverChunk(0, body, sizeof(body));
    }

    /// A POLL naming the image this fixture's offers declare (TEST_IMAGE_SHA).
    FirmwarePollPayload pollForCurrentImage() const {
        FirmwarePollPayload poll{};
        poll.command = static_cast<uint8_t>(FirmwareCmd::POLL);
        std::memcpy(poll.imageSha256, TEST_IMAGE_SHA, FIRMWARE_SHA256_LENGTH);
        return poll;
    }

    /// Every chunk index not yet landed, from the receive bitmap — what a
    /// real STATUS reply would report missing to a repair-round POLL.
    std::set<uint16_t> missingIndices() const {
        uint8_t received[FIRMWARE_BITMAP_BYTES];
        manager->fillBitmap(received);
        std::set<uint16_t> missing;
        for (uint16_t i = 0; i < FIRMWARE_MAX_CHUNKS; i++) {
            if ((received[i / 8] & (1 << (i % 8))) == 0) {
                missing.insert(i);
            }
        }
        return missing;
    }

    /// A signed offer for a synthetic image of `length` bytes split into
    /// `chunks` chunks of CHUNK_SIZE bytes each (the last one short).
    FirmwareOfferPayload signedOffer(uint32_t length, uint16_t chunks) {
        return buildSignedOffer(TEST_IMAGE_SHA, length, CHUNK_SIZE, chunks);
    }

    /// Like signedOffer, but with an explicit chunk size — for tests that
    /// need a geometry CHUNK_SIZE cannot express (e.g. the chunk-count
    /// ceiling with an image that still fits the slot).
    FirmwareOfferPayload signedOfferWithChunkSize(uint32_t length, uint16_t chunkSizeBytes, uint16_t chunks) {
        return buildSignedOffer(TEST_IMAGE_SHA, length, chunkSizeBytes, chunks);
    }

    /// A validly-signed offer whose chunkCount/chunkSize deliberately do not
    /// tile length, for the one test exercising the manager's own
    /// geometry-mismatch rejection. Every other builder here asserts the
    /// tiling instead, so a mismatch elsewhere reads as a fixture mistake.
    FirmwareOfferPayload signedOfferWithUncheckedGeometry(uint32_t length, uint16_t chunkSizeBytes,
                                                          uint16_t chunks) {
        FirmwareOfferPayload offer = firmware_test_keys::buildOffer(/*generation=*/1, TEST_IMAGE_SHA);
        offer.imageLength = length;
        offer.chunkSize = chunkSizeBytes;
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

    /// Delivers a raw radio frame through the same path the driver would
    /// use: the registered callback into dispatchPacket -> onPacketReceived,
    /// validating the frame's length before any cast to a wire struct.
    void deliverFrame(const uint8_t* data, size_t length) {
        if (rawHandler) {
            rawHandler(SEED_MAC, data, length, rawCtx);
        }
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
        return buildSignedOffer(hash, static_cast<uint32_t>(bytes.size()), CHUNK_SIZE, /*chunks=*/1);
    }

    ReceiverComms comms;
    FakeFirmwareStore store;
    FirmwareUpdateManager* manager;

private:
    // Builds a signed offer for `hash`, asserting that chunkSizeBytes/chunks
    // actually tile length: a mismatched pair here would silently produce an
    // offer the manager rejects for a reason the test never intended,
    // masking whatever the test meant to exercise.
    FirmwareOfferPayload buildSignedOffer(const uint8_t hash[FIRMWARE_SHA256_LENGTH], uint32_t length,
                                          uint16_t chunkSizeBytes, uint16_t chunks) {
        if (chunkSizeBytes != 0) {
            const uint64_t expected = (static_cast<uint64_t>(length) + chunkSizeBytes - 1) / chunkSizeBytes;
            if (expected != chunks) {
                std::fprintf(stderr,
                             "FirmwareReceiverFixture: chunkCount %u does not tile length %u at chunkSize"
                             " %u (expected %llu)\n",
                             chunks, length, chunkSizeBytes, static_cast<unsigned long long>(expected));
                std::abort();
            }
        }
        FirmwareOfferPayload offer = firmware_test_keys::buildOffer(/*generation=*/1, hash);
        offer.imageLength = length;
        offer.chunkSize = chunkSizeBytes;
        offer.chunkCount = chunks;
        resign(offer);
        return offer;
    }

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

    PeerCommsInterface::PacketCallback rawHandler;
    void* rawCtx = nullptr;
    FakeReceiverClock clock;
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
    // Fits the slot and tiles correctly at a smaller chunk size, so the
    // chunk-count ceiling is the only guard that can reject it — a
    // slot-size rejection here would hide a broken ceiling check.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC,
                       f.signedOfferWithChunkSize(/*length=*/3'994'000, /*chunkSizeBytes=*/1300, /*chunks=*/3073));
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

TEST(FirmwareReceiverTest, aChunkPastTheDeclaredImageLengthIsDropped) {
    // A geometry-valid offer still bounds each write against the declared
    // image length, not just against the chunk-count ceiling: an
    // unauthenticated CHUNK can carry any index/length pair that survives
    // the index < chunkCount check.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t body[1400];
    memset(body, 0xCD, sizeof(body));
    f.deliverChunk(/*index=*/3, body, sizeof(body));  // offset 4200 + 1400 > 5000
    EXPECT_EQ(f.manager->receivedCount(), 0);
    EXPECT_FALSE(f.store.wroteOutsideImage());
}

TEST(FirmwareReceiverTest, anOfferWithMismatchedGeometryIsIgnored) {
    // A validly-signed offer whose chunkCount/chunkSize do not tile
    // imageLength must still be refused — accepting it would let a later
    // CHUNK index request an offset far past the declared image.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC,
                       f.signedOfferWithUncheckedGeometry(/*length=*/5000, /*chunkSizeBytes=*/60000, /*chunks=*/4));
    EXPECT_FALSE(f.manager->isReceiving());
}

TEST(FirmwareReceiverTest, bitmapBitOrderIsLsbFirstWithinByte) {
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t body[1400] = {0};
    f.deliverChunk(2, body, sizeof(body));
    uint8_t bitmap[FIRMWARE_BITMAP_BYTES];
    f.manager->fillBitmap(bitmap);
    EXPECT_EQ(bitmap[0], 0x04);
}

TEST(FirmwareReceiverTest, aShortOfferFrameIsRejectedBeforeAnyCast) {
    // The buffer holds a fully valid signed offer; only the length passed to
    // deliverFrame claims otherwise. That isolates the length check itself —
    // every other guard would happily accept these bytes if given the real
    // length, so a garbage-filled short buffer would falsely appear to work.
    FirmwareReceiverFixture f;
    const FirmwareOfferPayload offer = f.signedOffer(5000, 4);
    uint8_t frame[sizeof(FirmwareOfferPayload)];
    memcpy(frame, &offer, sizeof(frame));
    f.deliverFrame(frame, /*length=*/4);
    EXPECT_FALSE(f.manager->isReceiving());
}

TEST(FirmwareReceiverTest, aShortChunkFrameIsRejectedBeforeAnyCast) {
    // Same technique as the OFFER case: a fully valid frame in the buffer,
    // a short claimed length.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t frame[sizeof(FirmwareChunkHeader) + 1400];
    FirmwareChunkHeader header{};
    header.command = static_cast<uint8_t>(FirmwareCmd::CHUNK);
    header.index = 1;
    header.length = 1400;
    memcpy(frame, &header, sizeof(header));
    memset(frame + sizeof(header), 0xEF, 1400);
    f.deliverFrame(frame, /*length=*/2);
    EXPECT_EQ(f.manager->receivedCount(), 0);
}

TEST(FirmwareReceiverTest, aWellFormedChunkFrameLandsViaTheWirePath) {
    // The positive case for the wire path: dispatchPacket must still deliver
    // a correctly-sized frame, not just reject short ones.
    FirmwareReceiverFixture f;
    f.manager->onOffer(SEED_MAC, f.signedOffer(5000, 4));
    uint8_t frame[sizeof(FirmwareChunkHeader) + 1400];
    FirmwareChunkHeader header{};
    header.command = static_cast<uint8_t>(FirmwareCmd::CHUNK);
    header.index = 1;
    header.length = 1400;
    memcpy(frame, &header, sizeof(header));
    memset(frame + sizeof(header), 0xEF, 1400);
    f.deliverFrame(frame, sizeof(frame));
    EXPECT_EQ(f.manager->receivedCount(), 1);
    EXPECT_EQ(f.store.slot()[1 * 1400], 0xEF);
}
