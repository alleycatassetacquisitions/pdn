#pragma once

#include <gtest/gtest.h>

#include <algorithm>
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
    int sendData(const uint8_t*, PktType, const uint8_t* data, const size_t length) override {
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
    void removePeer(uint8_t*) override {}
    /** Unused by the transport. */
    int addEspNowPeer(const uint8_t*) override { return 0; }
    /** Unused by the transport. */
    int removeEspNowPeer(const uint8_t*) override { return 0; }
    /** Unused by the transport. */
    void setPeerCommsState(PeerCommsState) override {}
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
        seed = new FirmwareUpdateManager(&seedRadio, &seedStore);
        receiver = new FirmwareUpdateManager(&receiverRadio, &receiverStore);
        receiver->setEligible(true);
    }
    /** Deletes both managers and unhooks the fake clock, which dies with the fixture. */
    ~FirmwareTransportFixture() {
        delete receiver;
        delete seed;
        SimpleTimer::setPlatformClock(nullptr);
    }

    /** Runs a seed run to its end, delivering every frame `drop` keeps; each tick is 100ms of fake time. */
    void runSeed(const std::function<bool(size_t frameNumber, const std::vector<uint8_t>& frame)>& drop = nullptr) {
        ASSERT_TRUE(seed->beginSeeding());
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

    /** Starts a seed run, lets it send `count` frames, and delivers them all to the receiver. */
    void deliverFirstFrames(size_t count) {
        ASSERT_TRUE(seed->beginSeeding());
        while (seedRadio.sent.size() < count) {
            clock.advance(100);
            seed->sync();
        }
        for (const std::vector<uint8_t>& frame : seedRadio.sent) {
            receiverRadio.deliver(frame);
        }
    }

    /** Lets `seconds` pass with no frame heard, syncing the receiver each second. */
    void hearNothingFor(int seconds) {
        for (int i = 0; i < seconds; i++) {
            clock.advance(1000);
            receiver->sync();
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
};

TEST(FirmwareTransportTest, aSeedSendsEveryChunkThreeTimesToAnIneligibleReceiver) {
    FirmwareTransportFixture f;
    f.receiver->setEligible(false);
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 0) << "an ineligible device takes no image";
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
    f.runSeed();

    EXPECT_EQ(f.receiverStore.slot, f.image);
    EXPECT_TRUE(f.receiverStore.bootSet);
    EXPECT_TRUE(f.receiverStore.restarted);
    // The store, not the transport, judges the image, from chunk 0 and the length.
    const std::vector<uint8_t> chunkZero(f.image.begin(), f.image.begin() + FIRMWARE_CHUNK_SIZE);
    EXPECT_EQ(f.receiverStore.lastHead, chunkZero);
    EXPECT_EQ(f.receiverStore.lastLength, FirmwareTransportFixture::IMAGE_LENGTH);
    EXPECT_FALSE(f.receiver->isSeeding()) << "only the hold gesture seeds; no frame can";
}

TEST(FirmwareTransportTest, chunksMissedInOnePassAreFilledFromTheNext) {
    FirmwareTransportFixture f;
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
    f.runSeed([&](size_t frameNumber, const std::vector<uint8_t>&) {
        return frameNumber < FirmwareTransportFixture::CHUNK_COUNT / 2;  // hears nothing at first
    });

    EXPECT_EQ(f.receiverStore.slot, f.image);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, waitingAPassForMissedChunksIsNotAStall) {
    // A receiver missing a late chunk hears only chunks it has until the seed comes round again.
    FirmwareTransportFixture f;
    std::map<uint16_t, int> seen;
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        const uint16_t index = FirmwareTransportFixture::chunkIndex(frame);
        if (seen[index]++ > 0) {
            // 700ms apart: ten of them outlast RECEIVE_STALL_MS, each inside BEACON_INTERVAL_MS.
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
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        f.receiverRadio.deliver(frame);  // every frame heard twice
        return false;
    });

    EXPECT_EQ(f.receiverStore.writeCount, FirmwareTransportFixture::CHUNK_COUNT);
    EXPECT_EQ(f.receiverStore.slot, f.image);
}

TEST(FirmwareTransportTest, theHeaderReachesFlashOnlyWithTheWholeImage) {
    // A reset mid-receive must leave nothing bootable in the spare slot.
    FirmwareTransportFixture f;
    f.runSeed([](size_t, const std::vector<uint8_t>& frame) {
        return FirmwareTransportFixture::chunkIndex(frame) == FirmwareTransportFixture::CHUNK_COUNT - 1;
    });

    EXPECT_GT(f.receiverStore.writeCount, 0);
    EXPECT_TRUE(std::all_of(f.receiverStore.slot.begin(), f.receiverStore.slot.begin() + FIRMWARE_CHUNK_SIZE,
                            [](uint8_t b) { return b == 0; }));
}

TEST(FirmwareTransportTest, anImageTheStoreRefusesAtChunkZeroIsNotWritten) {
    FirmwareTransportFixture f;
    f.receiverStore.wantImages = false;
    f.runSeed();

    EXPECT_EQ(f.receiverStore.writeCount, 0);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, aRefusedImageIsNotDownloadedAgainInTheSameRun) {
    FirmwareTransportFixture f;
    f.receiverStore.acceptImages = false;
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 1);
    EXPECT_FALSE(f.receiverStore.bootSet);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, aRefusedImageIsTriedAgainInALaterRun) {
    // One spoiled download, a forged chunk say, must not lock out the genuine image.
    FirmwareTransportFixture f;
    f.receiverStore.acceptImages = false;
    f.runSeed();
    f.hearNothingFor(10);
    f.receiverStore.acceptImages = true;
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 2);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, losingEligibilityDropsTheReceive) {
    FirmwareTransportFixture f;
    f.runSeed([&](size_t frameNumber, const std::vector<uint8_t>&) {
        if (frameNumber == 4) {
            f.receiver->setEligible(false);
        }
        return false;
    });

    EXPECT_EQ(f.receiverStore.abortCalls, 1);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, losingEligibilityBetweenChunksDropsTheReceive) {
    FirmwareTransportFixture f;
    f.deliverFirstFrames(3);
    ASSERT_EQ(f.receiverStore.beginWriteCalls, 1);

    f.receiver->setEligible(false);
    f.receiver->sync();

    EXPECT_EQ(f.receiverStore.abortCalls, 1);
}

TEST(FirmwareTransportTest, aSeedTakesNoImageWhileItSends) {
    FirmwareTransportFixture f;
    f.receiverStore.setRunningImage(std::vector<uint8_t>(64, 0x55), 0xB2);
    ASSERT_TRUE(f.receiver->beginSeeding());
    f.deliverFirstFrames(3);

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 0);
}

TEST(FirmwareTransportTest, theLastChunkAfterEligibilityEndsDoesNotInstall) {
    // Eligibility ends after the receiver's last sync() but before its final chunk.
    FirmwareTransportFixture f;
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        if (FirmwareTransportFixture::chunkIndex(frame) == FirmwareTransportFixture::CHUNK_COUNT - 1) {
            f.receiver->setEligible(false);
        }
        return false;
    });

    EXPECT_FALSE(f.receiverStore.restarted);
    EXPECT_EQ(f.receiverStore.abortCalls, 1);
}

TEST(FirmwareTransportTest, aReceiveWithNoChunksForTooLongIsDropped) {
    FirmwareTransportFixture f;
    f.runSeed([](size_t frameNumber, const std::vector<uint8_t>&) { return frameNumber > 3; });  // seed goes quiet
    f.hearNothingFor(20);

    EXPECT_EQ(f.receiverStore.abortCalls, 1);
    EXPECT_FALSE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, chunksOfAnotherImageAreIgnored) {
    FirmwareTransportFixture f;
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        if (FirmwareTransportFixture::chunkIndex(frame) == 0) {
            return false;
        }
        std::vector<uint8_t> foreign = frame;
        foreign[0] ^= 0xFF;  // another image's id
        f.receiverRadio.deliver(foreign);
        return true;
    });

    EXPECT_EQ(f.receiverStore.writeCount, 0) << "only chunk 0 belongs to the image being received, and it waits for the rest";
    EXPECT_FALSE(f.receiverStore.restarted);
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
    f.runSeed();

    EXPECT_FALSE(f.seed->isSeeding());
}

TEST(FirmwareTransportTest, anUnreadableImageIsNotSeeded) {
    FirmwareTransportFixture f;
    f.seedStore.setRunningImage({}, 0xA1);

    EXPECT_FALSE(f.seed->beginSeeding());
    EXPECT_FALSE(f.seed->isSeeding());
}
