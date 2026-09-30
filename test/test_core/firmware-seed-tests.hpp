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
#include <vector>

using ::testing::Invoke;
using ::testing::NiceMock;

/// Clock this suite drives by hand so offer cadence can be fast-forwarded
/// without a real 1Hz wait. Kept local to this file rather than reused from
/// utility-tests.hpp: this fixture's lifetime owns registering and clearing
/// SimpleTimer's global clock, and that ownership should not depend on
/// another test file's class staying available.
class FakeSeedClock : public PlatformClock {
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
    /// is a valid pointer rather than NiceMock's default nullptr.
    SeedComms() {
        ON_CALL(*this, getGlobalBroadcastAddress())
            .WillByDefault(Invoke([this]() -> const uint8_t* { return broadcastAddress; }));
    }

    /// Real behavior rather than a MOCK_METHOD: records every send so a
    /// pacing test can assert counts without an EXPECT_CALL per frame.
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
    /// Bytes in the fixture's synthetic running image: three full chunks,
    /// so a run exercises more than a single send.
    static constexpr size_t IMAGE_LENGTH = 3 * CHUNK_SIZE;

    /// Wires a fresh manager to a store already holding a synthetic running
    /// image and a trailer signed for it, and to a comms fake that counts
    /// sends instead of a gmock-expectation-driven one.
    FirmwareSeedFixture() {
        SimpleTimer::setPlatformClock(&clock);

        std::vector<uint8_t> image(IMAGE_LENGTH);
        for (size_t i = 0; i < image.size(); i++) {
            image[i] = static_cast<uint8_t>(i);
        }
        store.setRunningImage(image);
        store.setRunningTrailer(buildTrailer(image));

        manager = new FirmwareUpdateManager(&comms, &store, TEST_ROOT_PUBLIC_KEY);
    }

    /// Frees the manager this fixture owns and un-registers the fake clock
    /// so it does not outlive this fixture for a later test's SimpleTimer.
    ~FirmwareSeedFixture() {
        delete manager;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /// Advances the fake clock in small steps for `ms` milliseconds,
    /// calling sync() each step and immediately confirming whatever it
    /// sent — standing in for the radio's send-status report so streaming
    /// keeps moving instead of stalling after the first frame.
    void runSeedFor(unsigned long ms) {
        const unsigned long stepMs = 10;
        for (unsigned long elapsed = 0; elapsed < ms; elapsed += stepMs) {
            clock.advance(stepMs);
            manager->sync();
            manager->onSendReport(true);
        }
    }

    SeedComms comms;
    FakeFirmwareStore store;
    FirmwareUpdateManager* manager;

private:
    // Builds a trailer (SignerCert then a 64-byte image signature) that
    // signs exactly the span verifyOffer checks — imageSha256|imageLength|
    // chunkSize|chunkCount — for `image`, the same way sign_firmware.py
    // will sign a real build. Not asserted by this suite's tests, but a
    // trailer beginSeeding cannot read would fail every test here, and a
    // garbage one would misrepresent what production code actually does.
    std::vector<uint8_t> buildTrailer(const std::vector<uint8_t>& image) {
        uint8_t hash[FIRMWARE_SHA256_LENGTH];
        sha256(image.data(), image.size(), hash);

        FirmwareOfferPayload signedSpan{};
        std::memcpy(signedSpan.imageSha256, hash, FIRMWARE_SHA256_LENGTH);
        signedSpan.imageLength = static_cast<uint32_t>(image.size());
        signedSpan.chunkSize = CHUNK_SIZE;
        signedSpan.chunkCount = static_cast<uint16_t>((image.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);

        const uint8_t* signedStart =
            reinterpret_cast<const uint8_t*>(&signedSpan) + offsetof(FirmwareOfferPayload, imageSha256);
        const size_t signedLength =
            offsetof(FirmwareOfferPayload, cert) - offsetof(FirmwareOfferPayload, imageSha256);
        uint8_t imageSignature[FIRMWARE_SIG_LENGTH];
        firmware_test_keys::signRaw(firmware_test_keys::signerKeypair().privateKey, signedStart, signedLength,
                                    imageSignature);

        const SignerCert cert = firmware_test_keys::signCert(/*generation=*/1);
        std::vector<uint8_t> trailer(sizeof(SignerCert) + FIRMWARE_SIG_LENGTH);
        std::memcpy(trailer.data(), &cert, sizeof(SignerCert));
        std::memcpy(trailer.data() + sizeof(SignerCert), imageSignature, FIRMWARE_SIG_LENGTH);
        return trailer;
    }

    FakeSeedClock clock;
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
    FirmwareSeedFixture f;
    f.manager->beginSeeding();
    f.runSeedFor(3000);  // three seconds of streaming
    EXPECT_GE(f.comms.countOf(FirmwareCmd::OFFER), 3);
}
