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
#include <cstring>
#include <set>
#include <vector>

using ::testing::Invoke;
using ::testing::NiceMock;

/// Clock the firmware-distribution fixtures drive by hand so offer/poll
/// cadence can be fast-forwarded without a real wait. Shared by
/// FirmwareSeedFixture and FirmwareReceiverFixture (firmware-receiver-tests.hpp
/// includes this file for it) rather than duplicated per file: SimpleTimer's
/// clock is one static pointer, so a two-device test needs both fixtures
/// driving the *same* concrete clock object, not two independent ones.
class FakeClock : public PlatformClock {
public:
    /// Current simulated time in milliseconds.
    unsigned long milliseconds() override { return currentTime; }
    /// Moves the simulated clock forward by `delta` milliseconds.
    void advance(unsigned long delta) { currentTime += delta; }

private:
    unsigned long currentTime = 0;
};

/// Records every frame FirmwareUpdateManager sends. Overrides sendData with
/// real bookkeeping instead of leaving it a bare MOCK_METHOD, so a test can
/// assert counts directly rather than setting up gmock expectations per
/// send.
class SeedComms : public NiceMock<MockPeerComms> {
public:
    /// Stubs getGlobalBroadcastAddress so the destination sendData is given
    /// is a valid pointer rather than NiceMock's default nullptr, and
    /// getMacAddress so FirmwareUpdateManager's constructor (which seeds its
    /// per-device PRNG from it) has a real 6 bytes to fold rather than a
    /// null deref.
    SeedComms() {
        ON_CALL(*this, getGlobalBroadcastAddress())
            .WillByDefault(Invoke([this]() -> const uint8_t* { return broadcastAddress; }));
        ON_CALL(*this, getMacAddress()).WillByDefault(Invoke([this]() -> uint8_t* { return selfMac; }));
    }

    /// Overwrites the MAC getMacAddress() reports; must be called before the
    /// FirmwareUpdateManager under test is constructed, since it reads this
    /// only once, at construction.
    void setMacAddress(const uint8_t mac[6]) { std::memcpy(selfMac, mac, 6); }

    /// Real behavior rather than a MOCK_METHOD: records every send so a
    /// pacing test can assert counts without an EXPECT_CALL per frame.
    /// Honors refuseNextSend() first, standing in for a driver that refuses
    /// a frame synchronously (e.g. a ps_malloc failure) and so never queues
    /// it: no send-status report will ever arrive for a refused send.
    int sendData(const uint8_t*, PktType, const uint8_t* data, const size_t length) override {
        if (refuseNext) {
            refuseNext = false;
            return -1;
        }
        totalSent++;
        if (length > 0 && data[0] < commandCounts.size()) {
            commandCounts[data[0]]++;
        }
        if (length >= sizeof(FirmwareChunkHeader) && data[0] == static_cast<uint8_t>(FirmwareCmd::CHUNK)) {
            chunkIndices.insert(reinterpret_cast<const FirmwareChunkHeader*>(data)->index);
        }
        if (reportTarget != nullptr) {
            reportTarget->onSendReport(true);
        }
        return 0;
    }

    /// Count of every frame sent so far, of any command.
    int sentCount() const { return totalSent; }
    /// Count of frames sent so far whose leading command byte is `cmd`.
    int countOf(FirmwareCmd cmd) const { return commandCounts[static_cast<size_t>(cmd)]; }
    /// Every distinct chunk index sent so far, across both the initial
    /// stream and any repair rounds.
    const std::set<uint16_t>& chunkIndicesSent() const { return chunkIndices; }
    /// Forgets every chunk index recorded so far, so a caller can isolate
    /// what gets sent from this point on (e.g. one repair round) from
    /// whatever the initial stream already sent.
    void resetChunkIndicesSent() { chunkIndices.clear(); }
    /// Makes the next sendData call fail as if the driver refused to queue
    /// the frame, then reverts to succeeding.
    void refuseNextSend() { refuseNext = true; }

    /// Makes every sendData call from here on report completion to `manager`
    /// before it returns — the radio's callback landing inside the send call
    /// rather than after it, which is what the real driver can do.
    void reportFromInsideSend(FirmwareUpdateManager* manager) { reportTarget = manager; }

private:
    int totalSent = 0;
    std::array<int, 5> commandCounts{};
    std::set<uint16_t> chunkIndices;
    uint8_t broadcastAddress[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t selfMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF0};
    bool refuseNext = false;
    FirmwareUpdateManager* reportTarget = nullptr;
};

/// Shared by the seed suite: a manager wired to a fake flash slot holding a
/// running image plus a real signed trailer (cert then image signature),
/// and a comms fake that counts sent frames instead of requiring gmock
/// expectations per test.
class FirmwareSeedFixture {
public:
    /// Chunk size the production seed streams at (firmware-update-manager.cpp's
    /// SEED_CHUNK_SIZE); duplicated here so the fixture's trailer signs
    /// exactly the chunkSize/chunkCount the manager will reconstruct and
    /// offer, the same way a real build-time signer would.
    static constexpr uint16_t CHUNK_SIZE = 1400;
    /// Bytes in the fixture's synthetic running image: sixteen full chunks,
    /// enough headroom for a repair test to name any single-digit or
    /// low-teens chunk index without exceeding the image's real chunk count.
    static constexpr size_t IMAGE_LENGTH = 16 * CHUNK_SIZE;

    /// Wires a fresh manager to a store already holding a synthetic running
    /// image and a trailer signed for it, and to a comms fake that counts
    /// sends instead of a gmock-expectation-driven one. Installs `clock` as
    /// SimpleTimer's global clock.
    FirmwareSeedFixture()
        : FirmwareSeedFixture(nullptr) {}

    /// Like the default constructor, but drives `sharedClock` (another live
    /// fixture's `clock`) instead of installing its own — for a test where a
    /// seed and a receiver are two devices on one timeline, not two
    /// independent ones racing for SimpleTimer's single global clock.
    explicit FirmwareSeedFixture(FakeClock* sharedClock) {
        if (sharedClock != nullptr) {
            activeClock = sharedClock;
            ownsClock = false;
        } else {
            // SimpleTimer's clock is one static pointer: a second fixture
            // installing its own here would silently steal this one's
            // timeline out from under it (and whichever destructs first
            // would leave the survivor reading a null clock). Loud failure
            // instead of that.
            if (SimpleTimer::getPlatformClock() != nullptr) {
                // Not FAIL(): that macro expands to a `return`, which is not
                // legal in a constructor. ADD_FAILURE() records the same
                // failure without one.
                ADD_FAILURE() << "FirmwareSeedFixture: a clock is already installed by another live "
                                 "fixture; construct with FirmwareSeedFixture(FakeClock*) sharing that "
                                 "fixture's clock instead of two independent fixtures";
            }
            activeClock = &clock;
            ownsClock = true;
            SimpleTimer::setPlatformClock(activeClock);
        }

        std::vector<uint8_t> image(IMAGE_LENGTH);
        for (size_t i = 0; i < image.size(); i++) {
            image[i] = static_cast<uint8_t>(i);
        }
        store.setRunningImage(image);
        store.setRunningTrailer(buildTrailer(image));

        manager = new FirmwareUpdateManager(
            &comms, &store, TEST_ROOT_PUBLIC_KEY, []() { return true; }, DeviceType::PDN);
    }

    /// Frees the manager this fixture owns and, only if this fixture
    /// installed the clock itself (rather than sharing another's), clears
    /// SimpleTimer's global clock so it does not outlive this fixture.
    ~FirmwareSeedFixture() {
        delete manager;
        if (ownsClock) {
            SimpleTimer::setPlatformClock(nullptr);
        }
    }

    /// This fixture's own clock. Only the active timeline if constructed
    /// without a shared one — pass `&clock` to another fixture's
    /// shared-clock constructor to put both devices on this timeline.
    FakeClock clock;

    /// Advances the fake clock in small steps for `ms` milliseconds,
    /// calling sync() each step and immediately confirming whatever it
    /// sent — standing in for the radio's send-status report so streaming
    /// keeps moving instead of stalling after the first frame.
    void runSeedFor(unsigned long ms) {
        const unsigned long stepMs = 10;
        for (unsigned long elapsed = 0; elapsed < ms; elapsed += stepMs) {
            activeClock->advance(stepMs);
            manager->sync();
            manager->onSendReport(true);
        }
    }

    /// Drains the initial broadcast stream: every chunk of the fixture's
    /// image, in order. Leaves the seed free to move into its POLL/repair
    /// cadence on the next sync().
    void streamAllChunks() { drainSync(); }

    /// Drains half the initial broadcast stream, standing in for a run
    /// still in progress when a fresh device joins.
    void streamHalfTheChunks() {
        const uint16_t half = static_cast<uint16_t>((IMAGE_LENGTH / CHUNK_SIZE) / 2);
        for (uint16_t i = 0; i < half; i++) {
            activeClock->advance(10);
            manager->sync();
            manager->onSendReport(true);
        }
    }

    /// Runs exactly one POLL/window/resend cycle: finishes any initial
    /// streaming still pending, lets this round's POLL go out, clears the
    /// collection window, then drains whatever the round resends. Resets
    /// the comms fake's sent-chunk-index tracking first, so
    /// chunkIndicesSent() afterward reflects only chunks sent from this
    /// point on, not whatever the initial broadcast already sent.
    void runRepairRound() {
        comms.resetChunkIndicesSent();
        drainSync();
        activeClock->advance(SEED_POLL_WINDOW_MARGIN_MS);
        drainSync();
    }

    /// Folds a STATUS report into the run's repair set as if `missing` were
    /// the chunk indices the reporting device does not have yet. Bypasses
    /// the wire: onStatus doesn't take a sender MAC, so `fromMac` exists
    /// only to name which device a test means.
    void deliverStatus(const uint8_t* fromMac, const std::set<uint16_t>& missing) {
        deliverStatusForImage(fromMac, imageHash, missing);
    }

    /// Like deliverStatus, but for a caller-chosen image hash — the
    /// wrong-run case a real device's stale reply would carry.
    void deliverStatusForImage(const uint8_t*, const uint8_t hash[FIRMWARE_SHA256_LENGTH],
                               const std::set<uint16_t>& missing) {
        FirmwareStatusPayload status{};
        status.command = static_cast<uint8_t>(FirmwareCmd::STATUS);
        std::memcpy(status.imageSha256, hash, FIRMWARE_SHA256_LENGTH);
        std::memset(status.bitmap, 0xFF, sizeof(status.bitmap));
        for (uint16_t index : missing) {
            status.bitmap[index / 8] &= static_cast<uint8_t>(~(1u << (index % 8)));
        }
        manager->onStatus(status);
    }

    /// The signed OFFER this fixture's seed is currently broadcasting —
    /// same image hash, geometry, cert and signature sendOffer() builds.
    FirmwareOfferPayload currentOffer() const {
        FirmwareOfferPayload offer{};
        offer.command = static_cast<uint8_t>(FirmwareCmd::OFFER);
        std::memcpy(offer.imageSha256, imageHash, FIRMWARE_SHA256_LENGTH);
        offer.imageLength = static_cast<uint32_t>(IMAGE_LENGTH);
        offer.chunkSize = CHUNK_SIZE;
        offer.chunkCount = static_cast<uint16_t>(IMAGE_LENGTH / CHUNK_SIZE);
        offer.deviceType = static_cast<uint8_t>(DeviceType::PDN);
        offer.cert = cert;
        std::memcpy(offer.imageSignature, imageSignature, FIRMWARE_SIG_LENGTH);
        return offer;
    }

    SeedComms comms;
    FakeFirmwareStore store;
    FirmwareUpdateManager* manager;

private:
    // How long runRepairRound() advances the clock to clear the seed's
    // SEED_POLL_WINDOW_MS collection window with margin.
    static constexpr unsigned long SEED_POLL_WINDOW_MARGIN_MS = 700;

    // Drives sync()/onSendReport() in small steps until a call sends
    // nothing — the seed either ran out of work or is waiting on a timer
    // this fixture hasn't advanced past yet. Used for both the initial
    // stream and a repair round: neither needs a hardcoded chunk count to
    // know when it's done.
    void drainSync() {
        // beginSeeding() (or the previous drainSync() call's own last send)
        // may leave a frame in flight with no report yet; clear it before
        // measuring progress, or the very next sync() is a no-op purely
        // because that gate is still shut, and looks identical to the round
        // genuinely having nothing left to send.
        manager->onSendReport(true);
        for (int i = 0; i < 1000; i++) {
            const int before = comms.sentCount();
            activeClock->advance(10);
            manager->sync();
            manager->onSendReport(true);
            if (comms.sentCount() == before) {
                break;
            }
        }
    }

    // Builds a well-formed FirmwareTrailer (cert, image signature over
    // exactly the span verifyOffer checks, declared length, magic) the same
    // way sign_firmware.py will sign a real build, and caches the pieces
    // (imageHash/cert/imageSignature) so currentOffer() can hand out the
    // same signed offer the seed itself broadcasts.
    std::vector<uint8_t> buildTrailer(const std::vector<uint8_t>& image) {
        sha256(image.data(), image.size(), imageHash);

        FirmwareOfferPayload signedSpan{};
        std::memcpy(signedSpan.imageSha256, imageHash, FIRMWARE_SHA256_LENGTH);
        signedSpan.imageLength = static_cast<uint32_t>(image.size());
        signedSpan.chunkSize = CHUNK_SIZE;
        signedSpan.chunkCount = static_cast<uint16_t>((image.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
        // Must match the manager's own DeviceType (DeviceType::PDN): sendOffer
        // stamps outgoing offers with it, and this signature has to cover
        // whatever value actually goes out on the wire.
        signedSpan.deviceType = static_cast<uint8_t>(DeviceType::PDN);

        const uint8_t* signedStart =
            reinterpret_cast<const uint8_t*>(&signedSpan) + offsetof(FirmwareOfferPayload, imageSha256);
        const size_t signedLength =
            offsetof(FirmwareOfferPayload, cert) - offsetof(FirmwareOfferPayload, imageSha256);

        cert = firmware_test_keys::signCert(/*generation=*/1);
        firmware_test_keys::signRaw(firmware_test_keys::signerKeypair().privateKey, signedStart, signedLength,
                                    imageSignature);

        FirmwareTrailer trailer{};
        trailer.cert = cert;
        std::memcpy(trailer.imageSignature, imageSignature, FIRMWARE_SIG_LENGTH);
        trailer.imageLength = static_cast<uint32_t>(image.size());
        trailer.magic = FIRMWARE_TRAILER_MAGIC;

        std::vector<uint8_t> bytes(sizeof(FirmwareTrailer));
        std::memcpy(bytes.data(), &trailer, sizeof(FirmwareTrailer));
        return bytes;
    }

    uint8_t imageHash[FIRMWARE_SHA256_LENGTH] = {};
    SignerCert cert = {};
    uint8_t imageSignature[FIRMWARE_SIG_LENGTH] = {};

    // The clock this fixture actually drives: &clock when it installed its
    // own, or another fixture's clock when constructed to share one.
    // ownsClock says which, so only the fixture that installed a clock
    // clears it on destruction.
    FakeClock* activeClock = nullptr;
    bool ownsClock = false;
};

TEST(FirmwareSeedTest, holdsOneFrameInFlight) {
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    const int afterFirst = f.comms.sentCount();
    f.manager->sync();  // no report yet
    EXPECT_EQ(f.comms.sentCount(), afterFirst) << "seed ran ahead of the radio";
    f.manager->onSendReport(true);
    f.manager->sync();
    EXPECT_EQ(f.comms.sentCount(), afterFirst + 1);
}

TEST(FirmwareSeedTest, offerRepeatsThroughoutTheRun) {
    // A run with nothing left missing now ends (Task 10), so this keeps the
    // run alive across several repair rounds — a different chunk index each
    // time so the repair set never repeats and the run doesn't stall out —
    // rather than relying on an indefinite streaming phase.
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    f.streamAllChunks();
    f.deliverStatus(nullptr, {0});
    f.runRepairRound();
    f.deliverStatus(nullptr, {1});
    f.runRepairRound();
    f.deliverStatus(nullptr, {2});
    f.runRepairRound();
    f.deliverStatus(nullptr, {3});
    f.runRepairRound();
    EXPECT_GE(f.comms.countOf(FirmwareCmd::OFFER), 3);
}

TEST(FirmwareSeedTest, beginSeedingRefusesAnUnprovisionedTrailer) {
    FirmwareSeedFixture f;
    // Right size so the read itself succeeds — this isolates the
    // magic/imageLength validation from the bounds check on the read.
    f.store.setRunningTrailer(std::vector<uint8_t>(sizeof(FirmwareTrailer), 0));
    EXPECT_FALSE(f.manager->beginSeeding());
    EXPECT_FALSE(f.manager->isSeeding());
    EXPECT_EQ(f.comms.sentCount(), 0);
}

TEST(FirmwareSeedTest, aRefusedSendIsRetriedNotWedged) {
    FirmwareSeedFixture f;
    f.manager->beginSeeding();      // the initial OFFER goes out and succeeds
    f.manager->onSendReport(true);  // frees the slot
    f.comms.refuseNextSend();       // simulates a synchronous enqueue failure (e.g. ps_malloc)
    f.manager->sync();              // attempts the first chunk; refused, nothing queued
    EXPECT_EQ(f.comms.sentCount(), 1) << "a refused send must not count as sent";
    f.manager->sync();  // sendInFlight stayed false, so this retries
    EXPECT_EQ(f.comms.sentCount(), 2) << "the seed must retry a refused send, not wedge";
}

TEST(FirmwareSeedTest, aFailureReportFreesTheSendSlot) {
    // The radio reports failure once its retries run out, and that report is
    // the only thing that can free the slot: the IDF gives the callback no
    // frame identity, so nothing may time the slot out instead. A seed that
    // treated failure as "still in flight" would sit on UPDATING until it was
    // power-cycled.
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    const int afterFirst = f.comms.sentCount();
    f.manager->onSendReport(false);
    f.manager->sync();
    EXPECT_EQ(f.comms.sentCount(), afterFirst + 1) << "a failure report must free the send slot";
}

TEST(FirmwareSeedTest, aReportLandingInsideTheSendDoesNotWedgeTheSlot) {
    // The driver's send-status callback can run before sendData returns. An
    // in-flight flag latched after that call would overwrite the report's
    // clear, and no later report exists to free the slot: the seed would sit
    // on UPDATING until it was power-cycled.
    FirmwareSeedFixture f;
    f.comms.reportFromInsideSend(f.manager);
    f.manager->beginSeeding();
    const int afterFirst = f.comms.sentCount();

    f.manager->sync();  // no report from outside the send at all

    EXPECT_EQ(f.comms.sentCount(), afterFirst + 1) << "the send slot never reopened";
}
