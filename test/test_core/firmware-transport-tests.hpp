#pragma once

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <vector>

#include "device/firmware-update-manager.hpp"
#include "fake-firmware-store.hpp"
#include "utility-tests.hpp"

/** A radio that keeps every frame sent, with switches for a busy or refusing driver. */
class FakeFirmwareRadio : public PeerCommsInterface {
public:
    /** Keeps the frame unless refuseSends is set. */
    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) override {
        (void)dst;
        (void)packetType;
        if (refuseSends) {
            return -1;
        }
        sent.emplace_back(data, data + length);
        return 0;
    }
    /** Remembers the handler deliver() calls. */
    void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) override {
        packets[packetType] = {callback, ctx};
    }
    /** Forgets the handler. */
    void clearPacketHandler(PktType packetType) override { packets.erase(packetType); }
    /** queueEmpty, so a test can hold the seed back. */
    bool isSendQueueEmpty() override { return queueEmpty; }
    /** All-ones, as the driver's. */
    const uint8_t* getGlobalBroadcastAddress() override { return broadcast; }
    /** A fixed local MAC. */
    uint8_t* getMacAddress() override { return mac; }
    /** Unused by the transport. */
    void removePeer(uint8_t* macAddr) override { (void)macAddr; }
    /** Unused by the transport. */
    int addEspNowPeer(const uint8_t* macAddr) override {
        (void)macAddr;
        return 0;
    }
    /** Unused by the transport. */
    int removeEspNowPeer(const uint8_t* macAddr) override {
        (void)macAddr;
        return 0;
    }
    /** Unused by the transport. */
    void setPeerCommsState(PeerCommsState state) override { (void)state; }
    /** Always connected. */
    PeerCommsState getPeerCommsState() override { return PeerCommsState::CONNECTED; }
    /** Unused by the transport. */
    void connect() override {}
    /** Unused by the transport. */
    void disconnect() override {}

    /** Hands a frame to this radio's registered firmware handler. */
    void deliver(const std::vector<uint8_t>& frame) {
        auto handler = packets.find(PktType::kFirmwareUpdate);
        if (handler != packets.end()) {
            handler->second.first(mac, frame.data(), frame.size(), handler->second.second);
        }
    }

    std::vector<std::vector<uint8_t>> sent;
    bool queueEmpty = true;
    bool refuseSends = false;

private:
    std::map<PktType, std::pair<PacketCallback, void*>> packets;
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0x01};
};

/** A seed and one receiver, with a hand-driven link between them. */
class FirmwareTransportFixture {
public:
    static constexpr size_t IMAGE_LENGTH = 10 * FIRMWARE_CHUNK_SIZE + 123;  // last chunk short
    static constexpr uint16_t CHUNK_COUNT = 11;

    /** A seed running a 14,123-byte image and an eligible receiver. */
    FirmwareTransportFixture() {
        clock.setTime(1000);
        SimpleTimer::setPlatformClock(&clock);
        image.resize(IMAGE_LENGTH);
        for (size_t i = 0; i < image.size(); i++) {
            image[i] = static_cast<uint8_t>(i * 7 + 3);
        }
        seedStore.setRunningImage(image, 0xA1);
        seed = new FirmwareUpdateManager(&seedRadio, &seedStore, []() { return false; });
        receiver = new FirmwareUpdateManager(&receiverRadio, &receiverStore, [this]() { return eligible; });
    }
    /** Deletes both managers and unhooks the fake clock, which dies with the fixture. */
    ~FirmwareTransportFixture() {
        delete receiver;
        delete seed;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /**
     * Runs the seed until it stops, passing each frame to the receiver unless
     * `drop` says otherwise. Each frame takes 100ms of fake time, so this small
     * image's pass spans about one beacon interval.
     */
    void runSeed(const std::function<bool(size_t frameNumber, const std::vector<uint8_t>& frame)>& drop = nullptr) {
        size_t delivered = 0;
        for (int tick = 0; tick < 10000 && seed->isSeeding(); tick++) {
            clock.advance(100);
            seed->sync();
            receiver->sync();
            for (; delivered < seedRadio.sent.size(); delivered++) {
                // A real restart does not return; the fake's does, so stop listening.
                if (receiverStore.restarted) {
                    continue;
                }
                if (drop == nullptr || !drop(delivered, seedRadio.sent[delivered])) {
                    receiverRadio.deliver(seedRadio.sent[delivered]);
                }
            }
        }
    }

    /** The index a frame carries. */
    static uint16_t chunkIndex(const std::vector<uint8_t>& frame) {
        FirmwareChunkHeader header{};
        std::memcpy(&header, frame.data(), sizeof(header));
        return header.index;
    }

    FakePlatformClock clock;
    std::vector<uint8_t> image;
    FakeFirmwareRadio seedRadio;
    FakeFirmwareRadio receiverRadio;
    FakeFirmwareStore seedStore;
    FakeFirmwareStore receiverStore;
    FirmwareUpdateManager* seed = nullptr;
    FirmwareUpdateManager* receiver = nullptr;
    bool eligible = true;
};

TEST(FirmwareTransportTest, aSeedSendsEveryChunkThreeTimesThenStops) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.eligible = false;
    f.runSeed();

    EXPECT_FALSE(f.seed->isSeeding());
    std::map<uint16_t, int> sends;
    for (const auto& frame : f.seedRadio.sent) {
        sends[FirmwareTransportFixture::chunkIndex(frame)]++;
    }
    ASSERT_EQ(sends.size(), FirmwareTransportFixture::CHUNK_COUNT);
    EXPECT_GT(sends[0], 3) << "chunk 0 is also the once-a-second beacon";
    for (uint16_t index = 1; index < FirmwareTransportFixture::CHUNK_COUNT; index++) {
        EXPECT_EQ(sends[index], 3) << "chunk " << index;
    }
}

TEST(FirmwareTransportTest, aReceiverAssemblesTheImageAndRestartsIntoIt) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.slot, f.image);
    EXPECT_TRUE(f.receiverStore.bootSet);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, chunksMissedInOnePassAreFilledFromTheNext) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    std::map<uint16_t, int> seen;
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        const uint16_t index = FirmwareTransportFixture::chunkIndex(frame);
        return index != 0 && seen[index]++ == 0 && index % 3 == 0;  // lose some of the first pass
    });

    EXPECT_EQ(f.receiverStore.slot, f.image);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, aReceiverThatJoinsMidRunStillCompletes) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([&](size_t frameNumber, const std::vector<uint8_t>&) {
        return frameNumber < FirmwareTransportFixture::CHUNK_COUNT / 2;  // hears nothing at first
    });

    EXPECT_EQ(f.receiverStore.slot, f.image);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, waitingAPassForMissedChunksIsNotAStall) {
    // On the rig a pass of a 2.2MB image takes about 24s: a receiver missing a
    // late chunk hears only chunks it already has until the seed comes round again.
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    std::map<uint16_t, int> seen;
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        const uint16_t index = FirmwareTransportFixture::chunkIndex(frame);
        if (seen[index]++ > 0) {
            // Chunks it already has, spaced so the ten before the missed one
            // outlast the stall timeout but each arrives inside a beacon interval.
            f.clock.advance(600);
        }
        return index == FirmwareTransportFixture::CHUNK_COUNT - 1 && seen[index] == 1;
    });

    EXPECT_EQ(f.receiverStore.abortCalls, 0) << "the receive was dropped while the seed was still sending";
    EXPECT_EQ(f.receiverStore.beginWriteCalls, 1);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, eachChunkIsWrittenOnce) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        f.receiverRadio.deliver(frame);  // every frame heard twice
        return false;
    });

    EXPECT_EQ(f.receiverStore.writeCount, FirmwareTransportFixture::CHUNK_COUNT);
    EXPECT_EQ(f.receiverStore.slot, f.image);
}

TEST(FirmwareTransportTest, anImageTheStoreRefusesAtChunkZeroIsNotWritten) {
    // An older build, or one too large for the slot.
    FirmwareTransportFixture f;
    f.receiverStore.wantImages = false;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.writeCount, 0);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, theStoreJudgesChunkZeroAndTheImageLength) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    const std::vector<uint8_t> chunkZero(f.image.begin(), f.image.begin() + FIRMWARE_CHUNK_SIZE);
    EXPECT_EQ(f.receiverStore.lastHead, chunkZero);
    EXPECT_EQ(f.receiverStore.lastLength, FirmwareTransportFixture::IMAGE_LENGTH);
}

TEST(FirmwareTransportTest, aRefusedImageIsNotDownloadedAgainInTheSameRun) {
    FirmwareTransportFixture f;
    f.receiverStore.acceptImages = false;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 1);
    EXPECT_FALSE(f.receiverStore.bootSet);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, aRefusedImageIsTriedAgainInALaterRun) {
    // One spoiled download, a forged chunk for instance, must not shut the
    // genuine image out until reboot.
    FirmwareTransportFixture f;
    f.receiverStore.acceptImages = false;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();
    for (int i = 0; i < 10; i++) {
        f.clock.advance(1000);
        f.receiver->sync();
    }
    f.receiverStore.acceptImages = true;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 2);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, losingEligibilityDropsTheReceive) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([&](size_t frameNumber, const std::vector<uint8_t>&) {
        if (frameNumber == 4) {
            f.eligible = false;  // someone presses a button mid-transfer
        }
        return false;
    });

    EXPECT_EQ(f.receiverStore.abortCalls, 1);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, losingEligibilityBetweenChunksDropsTheReceive) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    while (f.seedRadio.sent.size() < 3) {
        f.clock.advance(100);
        f.seed->sync();
    }
    for (const std::vector<uint8_t>& frame : f.seedRadio.sent) {
        f.receiverRadio.deliver(frame);
    }
    ASSERT_EQ(f.receiverStore.beginWriteCalls, 1);

    f.eligible = false;
    f.receiver->sync();

    EXPECT_EQ(f.receiverStore.abortCalls, 1);
}

TEST(FirmwareTransportTest, aSeedTakesNoImageWhileItSends) {
    FirmwareTransportFixture f;
    f.receiverStore.setRunningImage(std::vector<uint8_t>(64, 0x55), 0xB2);
    ASSERT_TRUE(f.receiver->beginSeeding());
    ASSERT_TRUE(f.seed->beginSeeding());
    while (f.seedRadio.sent.size() < 3) {
        f.clock.advance(100);
        f.seed->sync();
    }
    for (const std::vector<uint8_t>& frame : f.seedRadio.sent) {
        f.receiverRadio.deliver(frame);
    }

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 0);
}

TEST(FirmwareTransportTest, theLastChunkAfterEligibilityEndsDoesNotInstall) {
    // The screen stops being interruptible, or a cable goes in, between the
    // receiver's sync() and the chunk that would complete the image.
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        if (FirmwareTransportFixture::chunkIndex(frame) == FirmwareTransportFixture::CHUNK_COUNT - 1) {
            f.eligible = false;
        }
        return false;
    });

    EXPECT_FALSE(f.receiverStore.restarted);
    EXPECT_EQ(f.receiverStore.abortCalls, 1);
}

TEST(FirmwareTransportTest, aReceiveWithNoChunksForTooLongIsDropped) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([](size_t frameNumber, const std::vector<uint8_t>&) { return frameNumber > 3; });  // seed goes quiet
    for (int i = 0; i < 20; i++) {
        f.clock.advance(1000);
        f.receiver->sync();
    }

    EXPECT_EQ(f.receiverStore.abortCalls, 1);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, chunksOfAnotherImageAreIgnored) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        if (FirmwareTransportFixture::chunkIndex(frame) == 0) {
            return false;
        }
        std::vector<uint8_t> foreign = frame;
        foreign[0] ^= 0xFF;  // another image's id
        f.receiverRadio.deliver(foreign);
        return true;
    });

    EXPECT_EQ(f.receiverStore.writeCount, 1) << "only chunk 0 belongs to the image being received";
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, anIneligibleDeviceTakesNoImage) {
    FirmwareTransportFixture f;
    f.eligible = false;  // mid-match, cabled, or someone at the screen
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 0);
}

TEST(FirmwareTransportTest, noFrameStartsASeedRun) {
    // Only the hold gesture seeds; nothing heard over the air can.
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();
    for (const auto& frame : f.seedRadio.sent) {
        f.receiverRadio.deliver(frame);
    }

    EXPECT_FALSE(f.receiver->isSeeding());
}

TEST(FirmwareTransportTest, aBusyDriverHoldsTheSeedBack) {
    FirmwareTransportFixture f;
    f.seedRadio.queueEmpty = false;
    ASSERT_TRUE(f.seed->beginSeeding());
    for (int i = 0; i < 50; i++) {
        f.clock.advance(100);
        f.seed->sync();
    }

    EXPECT_TRUE(f.seedRadio.sent.empty());
    EXPECT_TRUE(f.seed->isSeeding());
}

TEST(FirmwareTransportTest, aRadioThatRefusesEverythingStillEndsTheRun) {
    FirmwareTransportFixture f;
    f.seedRadio.refuseSends = true;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_FALSE(f.seed->isSeeding());
}

TEST(FirmwareTransportTest, anUnreadableImageIsNotSeeded) {
    FirmwareTransportFixture f;
    f.seedStore.setRunningImage({}, 0xA1);  // the store reports length 0 for an unreadable image

    EXPECT_FALSE(f.seed->beginSeeding());
    EXPECT_FALSE(f.seed->isSeeding());
}
