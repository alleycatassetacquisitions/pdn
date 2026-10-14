#pragma once

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <vector>

#include "device/firmware-update-manager.hpp"
#include "fake-firmware-store.hpp"
#include "utility-tests.hpp"

/**
 * A radio that keeps every frame sent and answers each one with a send report,
 * as the ESP-NOW driver does, so the seed's one-frame-in-flight gate reopens.
 */
class FakeFirmwareRadio : public PeerCommsInterface {
public:
    /** Keeps the frame and reports it sent. */
    int sendData(const uint8_t* dst, PktType packetType, const uint8_t* data, const size_t length) override {
        (void)dst;
        (void)packetType;
        sent.emplace_back(data, data + length);
        if (sendStatus.count(PktType::kFirmwareUpdate) != 0) {
            sendStatus[PktType::kFirmwareUpdate].first(nullptr, data, length, true,
                                                       sendStatus[PktType::kFirmwareUpdate].second);
        }
        return 0;
    }
    /** Remembers the handler deliver() calls. */
    void setPacketHandler(PktType packetType, PacketCallback callback, void* ctx) override {
        packets[packetType] = {callback, ctx};
    }
    /** Forgets the handler. */
    void clearPacketHandler(PktType packetType) override { packets.erase(packetType); }
    /** Remembers the handler sendData() reports to. */
    void setSendStatusHandler(PktType packetType, SendStatusCallback callback, void* ctx) override {
        sendStatus[packetType] = {callback, ctx};
    }
    /** Forgets the handler. */
    void clearSendStatusHandler(PktType packetType) override { sendStatus.erase(packetType); }
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

private:
    std::map<PktType, std::pair<PacketCallback, void*>> packets;
    std::map<PktType, std::pair<SendStatusCallback, void*>> sendStatus;
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0x01};
};

/** A seed and one receiver, with a hand-driven link between them. */
class FirmwareTransportFixture {
public:
    static constexpr size_t IMAGE_LENGTH = 10 * FIRMWARE_CHUNK_SIZE + 123;  // last chunk short
    static constexpr uint16_t CHUNK_COUNT = 11;

    /** A seed running a 14,123-byte image and an eligible receiver running another. */
    FirmwareTransportFixture() {
        clock.setTime(1000);
        SimpleTimer::setPlatformClock(&clock);
        image.resize(IMAGE_LENGTH);
        for (size_t i = 0; i < image.size(); i++) {
            image[i] = static_cast<uint8_t>(i * 7 + 3);
        }
        seedStore.setRunningImage(image, 0xA1);
        receiverStore.setRunningImage(std::vector<uint8_t>(64, 0x55), 0xB2);
        seed = new FirmwareUpdateManager(&seedRadio, &seedStore, []() { return false; });
        receiver = new FirmwareUpdateManager(&receiverRadio, &receiverStore, [this]() { return eligible; });
    }
    /** Deletes both managers. */
    ~FirmwareTransportFixture() {
        delete receiver;
        delete seed;
    }

    /**
     * Runs the seed until it stops, passing each frame to the receiver unless
     * `drop` says otherwise. Each frame takes 100ms of fake time, so with this
     * small image a pass spans about one offer interval, as a real pass spans
     * many.
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

    /** Whether a sent frame is a CHUNK. */
    static bool isChunk(const std::vector<uint8_t>& frame) {
        return frame[0] == static_cast<uint8_t>(FirmwareCmd::CHUNK);
    }
    /** The index a CHUNK frame carries. */
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
    int offers = 0;
    for (const auto& frame : f.seedRadio.sent) {
        if (FirmwareTransportFixture::isChunk(frame)) {
            sends[FirmwareTransportFixture::chunkIndex(frame)]++;
        } else {
            offers++;
        }
    }
    EXPECT_EQ(sends.size(), FirmwareTransportFixture::CHUNK_COUNT);
    for (const auto& entry : sends) {
        EXPECT_EQ(entry.second, 3) << "chunk " << entry.first;
    }
    EXPECT_GE(offers, 1);
}

TEST(FirmwareTransportTest, aReceiverAssemblesTheImageAndRestartsIntoIt) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.slot, f.image);
    EXPECT_TRUE(f.receiverStore.finished);
    EXPECT_TRUE(f.receiverStore.bootSet);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, chunksMissedInOnePassAreFilledFromTheNext) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    std::map<uint16_t, int> seen;
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        if (!FirmwareTransportFixture::isChunk(frame)) {
            return false;
        }
        const uint16_t index = FirmwareTransportFixture::chunkIndex(frame);
        return seen[index]++ == 0 && index % 3 == 0;  // lose a third of the first pass
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
    // On hardware a pass takes about 23s: a receiver missing early chunks hears
    // only chunks it already has until the seed comes round again.
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    std::map<uint16_t, int> seen;
    f.runSeed([&](size_t, const std::vector<uint8_t>& frame) {
        if (!FirmwareTransportFixture::isChunk(frame)) {
            return false;
        }
        const uint16_t index = FirmwareTransportFixture::chunkIndex(frame);
        if (seen[index]++ > 0) {
            f.clock.advance(2000);  // a long stretch of chunks it already has
        }
        // Misses the last chunk the first time, so the next pass repeats every
        // other chunk before it comes round.
        return index == FirmwareTransportFixture::CHUNK_COUNT - 1 && seen[index] == 1;
    });

    EXPECT_EQ(f.receiverStore.abortCalls, 0) << "the receive was dropped while the seed was still sending";
    EXPECT_EQ(f.receiverStore.beginWriteCalls, 1);
    EXPECT_TRUE(f.receiverStore.restarted);
}

TEST(FirmwareTransportTest, eachChunkIsWrittenOnce) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.receiverStore.acceptImages = false;  // keeps the receiver listening to every pass
    f.runSeed();

    EXPECT_EQ(f.receiverStore.writeCount, FirmwareTransportFixture::CHUNK_COUNT);
}

TEST(FirmwareTransportTest, theImageAlreadyRunningIsNotDownloaded) {
    FirmwareTransportFixture f;
    f.receiverStore.setRunningImage(f.image, 0xA1);
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 0);
}

TEST(FirmwareTransportTest, aRefusedImageIsNotDownloadedAgain) {
    FirmwareTransportFixture f;
    f.receiverStore.acceptImages = false;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 1);
    EXPECT_FALSE(f.receiverStore.bootSet);
    EXPECT_FALSE(f.receiverStore.restarted);
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

TEST(FirmwareTransportTest, aReceiveWithNoChunksForTooLongIsDropped) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([](size_t frameNumber, const std::vector<uint8_t>&) { return frameNumber > 3; });  // seed goes quiet
    for (int i = 0; i < 20; i++) {
        f.clock.advance(1000);
        f.receiver->sync();
    }

    EXPECT_FALSE(f.receiver->isReceiving());
    EXPECT_EQ(f.receiverStore.abortCalls, 1);
}

TEST(FirmwareTransportTest, anOfferTooLargeForTheSlotIsIgnored) {
    FirmwareTransportFixture f;
    f.receiverStore.inactiveSlotSize = FirmwareTransportFixture::IMAGE_LENGTH - 1;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed();

    EXPECT_EQ(f.receiverStore.beginWriteCalls, 0);
}

TEST(FirmwareTransportTest, chunksOfAnotherImageAreIgnored) {
    FirmwareTransportFixture f;
    ASSERT_TRUE(f.seed->beginSeeding());
    f.runSeed([](size_t, const std::vector<uint8_t>& frame) {
        if (FirmwareTransportFixture::isChunk(frame)) {
            const_cast<std::vector<uint8_t>&>(frame)[1] ^= 0xFF;  // another run's image id
        }
        return false;
    });

    EXPECT_EQ(f.receiverStore.writeCount, 0);
}

TEST(FirmwareTransportTest, anIneligibleDeviceTakesNoOffer) {
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
