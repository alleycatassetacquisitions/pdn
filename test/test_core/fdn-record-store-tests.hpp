#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "alleycat-server/fdn-record-store.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device-mock.hpp"
#include "game/match.hpp"
#include "utility-tests.hpp"
#include "wireless/record-relay-types.hpp"

/** One stored acknowledgement as the FDN's radio was asked to send it. */
struct SentRecordAck {
    PktType type;
    std::array<uint8_t, 6> toMac;
    size_t length;
    uint8_t kind;
    uint32_t recordNumber;
    std::array<uint8_t, RECORD_CHECK_SIZE> recordCheck;
    /** Records of the acknowledged kind the store held at the moment of the send. */
    size_t storedWhenSent;
};

/** In-memory storage that counts its writes and can be told to refuse them. */
class RefusingPrefsDriver : public NativePrefsDriver {
public:
    using NativePrefsDriver::NativePrefsDriver;

    /** Counts the write; refuses it while refuseBlobWrites is set. */
    size_t writeBytes(const std::string& ns, const std::string& key, const uint8_t* data,
                      size_t length) override {
        writes++;
        if (refuseBlobWrites) return 0;
        return NativePrefsDriver::writeBytes(ns, key, data, length);
    }

    /** Counts the write; refuses it when @p key is refusedCounterKey. */
    size_t writeUInt(const std::string& ns, const std::string& key, uint32_t value) override {
        writes++;
        if (key == refusedCounterKey) return 0;
        return NativePrefsDriver::writeUInt(ns, key, value);
    }

    /** True when a blob is stored under @p key. */
    bool holdsBlob(const std::string& ns, const std::string& key) {
        uint8_t blob[256] = {};
        return readBytes(ns, key, blob, sizeof(blob)) > 0;
    }

    bool refuseBlobWrites = false;
    std::string refusedCounterKey;
    size_t writes = 0;
};

/**
 * An FDN with nothing Connected on its jacks, a record store on in-memory
 * storage, a radio that records every stored acknowledgement it is given, and
 * an HTTP client that records every request.
 */
class FdnRecordStoreTests : public testing::Test {
public:
    /** Builds the store with the peer-to-peer radio enabled. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*fdnDevice.mockPeerComms, getPeerCommsState())
        .WillByDefault(testing::Return(PeerCommsState::ENABLED));
        ON_CALL(*fdnDevice.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(
        [this](const uint8_t* toMac, PktType type, const uint8_t* data, const size_t length) {
            noteSentFrame(toMac, type, data, length);
            return 1;
        });
        ON_CALL(*fdnDevice.mockHttpClient, queueRequest(testing::_))
        .WillByDefault([this](HttpRequest&& request) {
            httpRequests.push_back(std::move(request));
            return true;
        });

        fdnRecordStore = new FdnRecordStore(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);
    }

    /** Releases the store before the radio it is registered on. */
    void TearDown() override {
        delete fdnRecordStore;
        fdnRecordStore = nullptr;
        httpRequests.clear();
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** Replaces the store with a fresh one on the same storage, as a reboot does. */
    void restartStore() {
        delete fdnRecordStore;
        fdnRecordStore = nullptr;
        fdnRecordStore = new FdnRecordStore(fdnDevice.wirelessManager, &fdnPrefs, &fdnRdc);
    }

    /** Uploads the stored match records, answers with success, and hands the radio back. */
    bool uploadMatchRecords() {
        const size_t requestsBefore = httpRequests.size();
        if (!fdnRecordStore->uploadPendingBatch(nullptr)) return false;
        if (httpRequests.size() != requestsBefore + 1) return false;
        httpRequests.back().onSuccess("");
        fdnDevice.wirelessManager->enablePeerCommsMode();
        return true;
    }

    /** A crash record frame whose record bytes are all @p fill, with no record key. */
    static CrashRecordFramePacket crashFrame(uint32_t recordNumber, uint8_t fill) {
        CrashRecordFramePacket frame{};
        frame.kind = static_cast<uint8_t>(RecordKind::CRASH);
        frame.recordNumber = recordNumber;
        memset(frame.recordBytes, fill, sizeof(frame.recordBytes));
        return frame;
    }

    /** A match record frame whose record bytes are all @p fill, with no record key. */
    static MatchRecordFramePacket matchFrame(uint32_t recordNumber, uint8_t fill) {
        MatchRecordFramePacket frame{};
        frame.kind = static_cast<uint8_t>(RecordKind::MATCH);
        frame.recordNumber = recordNumber;
        memset(frame.recordBytes, fill, sizeof(frame.recordBytes));
        return frame;
    }

    /** A match record frame carrying a real match, which the upload can encode. */
    static MatchRecordFramePacket uploadableMatchFrame(uint32_t recordNumber) {
        char matchId[37];
        snprintf(matchId, sizeof(matchId), "%08u-abcd-4000-8000-0123456789ab", recordNumber);
        const Match match(matchId);
        MatchRecordFramePacket frame = matchFrame(recordNumber, 0);
        match.serialize(frame.recordBytes);
        return frame;
    }

    /**
     * Hands the FDN @p length bytes of @p frame on packet type @p type as if
     * @p fromMac had sent them. Every delivery is a new send, so it carries a
     * new sequence ID, as a PDN's resend on a later beacon does.
     */
    template <class Frame>
    void deliver(PktType type, const uint8_t* fromMac, Frame frame, size_t length = sizeof(Frame)) {
        nextSeqId = nextSeqId == 255 ? 1 : nextSeqId + 1;
        frame.seqId = nextSeqId;
        fdnDevice.mockPeerComms->triggerPacketCallback(
        fromMac, type, reinterpret_cast<const uint8_t*>(&frame), length);
    }

    /** The record check over @p frame's record bytes and trailer. */
    template <class Frame>
    static std::array<uint8_t, RECORD_CHECK_SIZE> recordCheckOf(const Frame& frame) {
        uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
        computeRecordCheck(frame.recordBytes, sizeof(frame.recordBytes), frame.trailer,
                           recordCheck);
        std::array<uint8_t, RECORD_CHECK_SIZE> result{};
        memcpy(result.data(), recordCheck, RECORD_CHECK_SIZE);
        return result;
    }

    /**
     * True when the FDN treats @p frame from @p fromMac as one it already holds:
     * it acknowledges the frame and stores nothing new.
     */
    template <class Frame>
    bool isRecognisedAsHeld(PktType type, RecordKind kind, const uint8_t* fromMac,
                            const Frame& frame) {
        const size_t acksBefore = sentAcks.size();
        const size_t storedBefore = fdnRecordStore->getStoredCount(kind);
        deliver(type, fromMac, frame);
        return sentAcks.size() == acksBefore + 1 &&
               fdnRecordStore->getStoredCount(kind) == storedBefore;
    }

    FakePlatformClock* fakeClock = nullptr;
    MockDevice fdnDevice;
    FakeRemoteDeviceCoordinator fdnRdc;
    RefusingPrefsDriver fdnPrefs{"fdn-record-prefs", {FDN_RECORD_STORE_NAMESPACE}};
    FdnRecordStore* fdnRecordStore = nullptr;
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x31};
    uint8_t otherPdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x32};
    std::vector<SentRecordAck> sentAcks;
    std::vector<HttpRequest> httpRequests;
    size_t collectionAcksSent = 0;
    uint8_t nextSeqId = 0;

private:
    void noteSentFrame(const uint8_t* toMac, PktType type, const uint8_t* data, size_t length) {
        if (type == PktType::kMatchCollectionAck) {
            collectionAcksSent++;
            return;
        }
        if (type != PktType::kCrashRecordAck && type != PktType::kMatchRecordAck) return;

        SentRecordAck sent{};
        sent.type = type;
        memcpy(sent.toMac.data(), toMac, 6);
        sent.length = length;
        if (length == sizeof(RecordAckPacket)) {
            RecordAckPacket ack{};
            memcpy(&ack, data, sizeof(ack));
            sent.kind = ack.kind;
            sent.recordNumber = ack.recordNumber;
            memcpy(sent.recordCheck.data(), ack.recordCheck, RECORD_CHECK_SIZE);
        }
        if (fdnRecordStore != nullptr) {
            sent.storedWhenSent = fdnRecordStore->getStoredCount(
            type == PktType::kCrashRecordAck ? RecordKind::CRASH : RecordKind::MATCH);
        }
        sentAcks.push_back(sent);
    }
};

inline std::array<uint8_t, 6> macArray(const uint8_t* mac) {
    std::array<uint8_t, 6> result{};
    memcpy(result.data(), mac, 6);
    return result;
}

inline void fdnStoresThenAcknowledgesARecordFromAPdnThatIsNotConnected(FdnRecordStoreTests* suite) {
    CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(7, 0x5A);
    crash.trailer.keyId = 3;
    memset(crash.trailer.tag, 0xC4, sizeof(crash.trailer.tag));
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 1u);
    EXPECT_EQ(suite->sentAcks[0].type, PktType::kCrashRecordAck);
    EXPECT_EQ(suite->sentAcks[0].toMac, macArray(suite->pdnMac));
    EXPECT_EQ(suite->sentAcks[0].length, sizeof(RecordAckPacket));
    EXPECT_EQ(suite->sentAcks[0].kind, 0x01);
    EXPECT_EQ(suite->sentAcks[0].recordNumber, 7u);
    EXPECT_EQ(suite->sentAcks[0].recordCheck, FdnRecordStoreTests::recordCheckOf(crash));
    EXPECT_EQ(suite->sentAcks[0].storedWhenSent, 1u)
    << "the acknowledgement went out before the record was stored";

    const MatchRecordFramePacket match = FdnRecordStoreTests::matchFrame(12, 0x3C);
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, match);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 2u);
    EXPECT_EQ(suite->sentAcks[1].type, PktType::kMatchRecordAck);
    EXPECT_EQ(suite->sentAcks[1].toMac, macArray(suite->pdnMac));
    EXPECT_EQ(suite->sentAcks[1].kind, 0x02);
    EXPECT_EQ(suite->sentAcks[1].recordNumber, 12u);
    EXPECT_EQ(suite->sentAcks[1].recordCheck, FdnRecordStoreTests::recordCheckOf(match));
    EXPECT_EQ(suite->sentAcks[1].storedWhenSent, 1u);
}

inline void fdnKeepsBothKindsAcrossARestart(FdnRecordStoreTests* suite) {
    CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(4, 0x41);
    crash.trailer.keyId = 2;
    memset(crash.trailer.tag, 0x77, sizeof(crash.trailer.tag));
    const MatchRecordFramePacket match = FdnRecordStoreTests::matchFrame(9, 0x42);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    suite->deliver(PktType::kMatchRecord, suite->otherPdnMac, match);
    ASSERT_EQ(suite->sentAcks.size(), 2u);

    suite->restartStore();

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, crash))
    << "a record held before the restart is no longer known";
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->otherPdnMac, match));

    // The sender is part of what was kept: the same frame from another PDN is a new record.
    suite->deliver(PktType::kCrashRecord, suite->otherPdnMac, crash);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u);

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(5, 0x43));
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 3u);
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, crash))
    << "a record stored after the restart overwrote one stored before it";
}

inline void fdnDropsARecordFrameWhoseKindIsNotTheChannels(FdnRecordStoreTests* suite) {
    CrashRecordFramePacket markedMatch = FdnRecordStoreTests::crashFrame(1, 0x21);
    markedMatch.kind = static_cast<uint8_t>(RecordKind::MATCH);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, markedMatch);

    CrashRecordFramePacket unknownKind = FdnRecordStoreTests::crashFrame(2, 0x22);
    unknownKind.kind = 0x7F;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, unknownKind);

    MatchRecordFramePacket markedCrash = FdnRecordStoreTests::matchFrame(3, 0x23);
    markedCrash.kind = static_cast<uint8_t>(RecordKind::CRASH);
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, markedCrash);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 0u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
    EXPECT_TRUE(suite->sentAcks.empty());
}

inline void fdnDropsARecordFrameOfTheWrongLength(FdnRecordStoreTests* suite) {
    // Marked as a crash record, as long as a match record frame.
    MatchRecordFramePacket crashAtMatchLength = FdnRecordStoreTests::matchFrame(1, 0x31);
    crashAtMatchLength.kind = static_cast<uint8_t>(RecordKind::CRASH);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crashAtMatchLength);

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(2, 0x32),
                   sizeof(CrashRecordFramePacket) - 1);

    CrashRecordFramePacket matchAtCrashLength = FdnRecordStoreTests::crashFrame(3, 0x33);
    matchAtCrashLength.kind = static_cast<uint8_t>(RecordKind::MATCH);
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, matchAtCrashLength);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 0u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
    EXPECT_TRUE(suite->sentAcks.empty());
}

inline void fdnAcknowledgesAResentRecordAndStoresItOnce(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(5, 0x44);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 2u);
    EXPECT_EQ(suite->sentAcks[1].type, PktType::kCrashRecordAck);
    EXPECT_EQ(suite->sentAcks[1].recordNumber, 5u);
    EXPECT_EQ(suite->sentAcks[1].recordCheck, FdnRecordStoreTests::recordCheckOf(crash));
}

inline void fdnStoresADifferentRecordUnderAHeldNumberSeparately(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket first = FdnRecordStoreTests::crashFrame(5, 0x51);
    const CrashRecordFramePacket otherBytes = FdnRecordStoreTests::crashFrame(5, 0x52);
    CrashRecordFramePacket otherTrailer = first;
    otherTrailer.trailer.keyId = 2;
    memset(otherTrailer.trailer.tag, 0x99, sizeof(otherTrailer.trailer.tag));

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, first);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, otherBytes);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, otherTrailer);
    suite->deliver(PktType::kCrashRecord, suite->otherPdnMac, first);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 4u);
    ASSERT_EQ(suite->sentAcks.size(), 4u);
    EXPECT_EQ(suite->sentAcks[0].recordCheck, FdnRecordStoreTests::recordCheckOf(first));
    EXPECT_EQ(suite->sentAcks[1].recordCheck, FdnRecordStoreTests::recordCheckOf(otherBytes));
    EXPECT_EQ(suite->sentAcks[2].recordCheck, FdnRecordStoreTests::recordCheckOf(otherTrailer));
    EXPECT_NE(suite->sentAcks[0].recordCheck, suite->sentAcks[1].recordCheck);
    EXPECT_NE(suite->sentAcks[0].recordCheck, suite->sentAcks[2].recordCheck);

    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, first))
    << "the first record was displaced";
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, otherBytes));
    EXPECT_TRUE(suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac,
                                          otherTrailer));
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->otherPdnMac, first));
}

inline void fdnWithFullCrashStorageAcknowledgesOnlyWhatItAlreadyHolds(FdnRecordStoreTests* suite) {
    for (uint32_t number = 1; number <= FDN_CRASH_RECORD_CAPACITY; ++number) {
        suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                       FdnRecordStoreTests::crashFrame(number, static_cast<uint8_t>(number)));
    }
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 32u);
    ASSERT_EQ(suite->sentAcks.size(), 32u);

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(33, 33));
    EXPECT_EQ(suite->sentAcks.size(), 32u) << "a record that could not be stored was acknowledged";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 32u);

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(1, 1));
    ASSERT_EQ(suite->sentAcks.size(), 33u) << "a duplicate is acknowledged even when full";
    EXPECT_EQ(suite->sentAcks[32].recordNumber, 1u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 32u);

    suite->deliver(PktType::kMatchRecord, suite->pdnMac, FdnRecordStoreTests::matchFrame(1, 0x61));
    ASSERT_EQ(suite->sentAcks.size(), 34u) << "full crash storage held back a match record";
    EXPECT_EQ(suite->sentAcks[33].type, PktType::kMatchRecordAck);
}

inline void fdnWithFullMatchStorageSendsNoAcknowledgement(FdnRecordStoreTests* suite) {
    for (uint32_t number = 1; number <= FDN_MATCH_RECORD_CAPACITY; ++number) {
        suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                       FdnRecordStoreTests::matchFrame(number, static_cast<uint8_t>(number)));
    }
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 64u);
    ASSERT_EQ(suite->sentAcks.size(), 64u);

    suite->deliver(PktType::kMatchRecord, suite->pdnMac, FdnRecordStoreTests::matchFrame(65, 65));
    EXPECT_EQ(suite->sentAcks.size(), 64u) << "a record that could not be stored was acknowledged";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 64u);

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(1, 0x62));
    ASSERT_EQ(suite->sentAcks.size(), 65u) << "full match storage held back a crash record";
    EXPECT_EQ(suite->sentAcks[64].type, PktType::kCrashRecordAck);
}

inline void fdnStoresAndAcknowledgesARecordWithNoRecordKey(FdnRecordStoreTests* suite) {
    CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(9, 0);
    for (size_t i = 0; i < sizeof(crash.recordBytes); ++i) {
        crash.recordBytes[i] = static_cast<uint8_t>(i);
    }
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 1u);
    // First 8 bytes of SHA-256 over bytes 0..41 then 17 zero bytes, from Python's hashlib.
    const std::array<uint8_t, RECORD_CHECK_SIZE> expected = {0x1e, 0xa6, 0x48, 0x6a,
                                                             0x62, 0x39, 0x13, 0xd7};
    EXPECT_EQ(suite->sentAcks[0].recordCheck, expected);
    EXPECT_EQ(suite->sentAcks[0].recordNumber, 9u);

    // What was kept is the zero trailer: the same record under a key ID is another record.
    suite->restartStore();
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, crash));
    CrashRecordFramePacket underKeyOne = crash;
    underKeyOne.trailer.keyId = 1;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, underKeyOne);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u);
}

// The tag here is arbitrary bytes, so it cannot match the record; the FDN has
// no key to find that out with and must keep it as it came.
inline void fdnStoresARecordWithABadTagUnchanged(FdnRecordStoreTests* suite) {
    MatchRecordFramePacket match = FdnRecordStoreTests::matchFrame(4, 0);
    for (size_t i = 0; i < sizeof(match.recordBytes); ++i) {
        match.recordBytes[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    match.trailer.keyId = 5;
    for (size_t i = 0; i < sizeof(match.trailer.tag); ++i) {
        match.trailer.tag[i] = static_cast<uint8_t>(0xA0 + i);
    }
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, match);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 1u);
    // First 8 bytes of SHA-256 over those record bytes then that trailer, from Python's hashlib.
    const std::array<uint8_t, RECORD_CHECK_SIZE> expected = {0xee, 0x02, 0xc6, 0x39,
                                                             0xae, 0xfa, 0x7f, 0xc3};
    EXPECT_EQ(suite->sentAcks[0].recordCheck, expected);
    EXPECT_EQ(suite->sentAcks[0].type, PktType::kMatchRecordAck);

    // Read back from storage, the record still matches the frame byte for byte,
    // and one differing tag byte makes a different record.
    suite->restartStore();
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->pdnMac, match))
    << "the record or its trailer was altered on the way into storage";
    MatchRecordFramePacket otherTag = match;
    otherTag.trailer.tag[RECORD_TAG_SIZE - 1] ^= 0x01;
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, otherTag);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);
}

inline void fdnAsksRefusingStorageOnlyOnceUntilASlotIsFreed(FdnRecordStoreTests* suite) {
    const MatchRecordFramePacket first = FdnRecordStoreTests::uploadableMatchFrame(1);
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, first);
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(2));
    ASSERT_EQ(suite->sentAcks.size(), 2u);

    suite->fdnPrefs.refuseBlobWrites = true;
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(3));
    EXPECT_EQ(suite->sentAcks.size(), 2u) << "a record the storage refused was acknowledged";
    const size_t writesAfterRefusal = suite->fdnPrefs.writes;

    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(3));
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(4));
    // Storage would take a write again, but nothing has been freed to make room.
    suite->fdnPrefs.refuseBlobWrites = false;
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(5));
    EXPECT_EQ(suite->fdnPrefs.writes, writesAfterRefusal)
    << "storage that refused a record was asked again";
    EXPECT_EQ(suite->sentAcks.size(), 2u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);

    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->pdnMac, first))
    << "a held record stopped being acknowledged while storage refused new ones";
    EXPECT_EQ(suite->fdnPrefs.writes, writesAfterRefusal);

    ASSERT_TRUE(suite->uploadMatchRecords());
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);

    const size_t acksBeforeRetry = suite->sentAcks.size();
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(3));
    EXPECT_EQ(suite->sentAcks.size(), acksBeforeRetry + 1)
    << "freed slots did not let the store take records again";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
}

inline void fdnLeavesNoOrphanWhenTheRecordIsWrittenButNotCommitted(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(1, 0x10);
    suite->fdnPrefs.refusedCounterKey = "c_tail";
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);

    EXPECT_TRUE(suite->sentAcks.empty()) << "a record that was never committed was acknowledged";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 0u);
    EXPECT_FALSE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "c_0"))
    << "the uncommitted record was left in storage where nothing would ever retire it";

    // Storage would take a write again, but the store does not ask it.
    suite->fdnPrefs.refusedCounterKey.clear();
    const size_t writesAfterRefusal = suite->fdnPrefs.writes;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(2, 0x11));
    EXPECT_EQ(suite->fdnPrefs.writes, writesAfterRefusal)
    << "storage that refused the commit was asked again";
    EXPECT_TRUE(suite->sentAcks.empty());

    suite->restartStore();
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    EXPECT_EQ(suite->sentAcks.size(), 1u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
}

inline void fdnErasesAnUploadedBatchEvenWhenTheHeadCannotAdvance(FdnRecordStoreTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(2));
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);

    suite->fdnPrefs.refusedCounterKey = "m_head";
    ASSERT_TRUE(suite->uploadMatchRecords());
    EXPECT_FALSE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "m_0"))
    << "an uploaded record stayed in storage";
    EXPECT_FALSE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "m_1"));
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u)
    << "erased records still count as held";

    const size_t acksAfterUpload = suite->sentAcks.size();
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(3));
    EXPECT_EQ(suite->sentAcks.size(), acksAfterUpload + 1)
    << "the room the upload freed was not usable straight away";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);

    // A restart reads the head that was never stored, which rests on the erased
    // slots; the next upload drops them instead of stalling behind them.
    suite->fdnPrefs.refusedCounterKey.clear();
    suite->restartStore();
    ASSERT_TRUE(suite->uploadMatchRecords());
    ASSERT_EQ(suite->httpRequests.size(), 2u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
    EXPECT_FALSE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "m_2"));
}

// The rings share one partition, so room one of them frees is room for the other.
inline void fdnMatchUploadThatFreesSlotsRestoresCrashIntake(FdnRecordStoreTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    ASSERT_EQ(suite->sentAcks.size(), 1u);

    const CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(1, 0x12);
    suite->fdnPrefs.refuseBlobWrites = true;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    EXPECT_EQ(suite->sentAcks.size(), 1u) << "a crash record the storage refused was acknowledged";

    suite->fdnPrefs.refuseBlobWrites = false;
    const size_t writesAfterRefusal = suite->fdnPrefs.writes;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    EXPECT_EQ(suite->fdnPrefs.writes, writesAfterRefusal)
    << "storage that refused a crash record was asked again before anything was freed";
    EXPECT_EQ(suite->sentAcks.size(), 1u);

    ASSERT_TRUE(suite->uploadMatchRecords());

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    ASSERT_EQ(suite->sentAcks.size(), 2u)
    << "slots freed by the match upload did not restore crash intake";
    EXPECT_EQ(suite->sentAcks[1].type, PktType::kCrashRecordAck);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
}

// Sender, number and record check all still match what the store remembers,
// so only the bytes in storage can say this is no longer the record it holds.
inline void fdnTreatsAResendAsNewWhenTheStoredBytesNoLongerMatch(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(3, 0x70);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);

    uint8_t blob[256] = {};
    const size_t blobLength =
    suite->fdnPrefs.readBytes(FDN_RECORD_STORE_NAMESPACE, "c_0", blob, sizeof(blob));
    ASSERT_GT(blobLength, 0u);
    blob[blobLength - 1] ^= 0x01;
    ASSERT_EQ(suite->fdnPrefs.writeBytes(FDN_RECORD_STORE_NAMESPACE, "c_0", blob, blobLength),
              blobLength);

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u)
    << "a frame was taken for a duplicate of stored bytes it does not match";
    EXPECT_EQ(suite->sentAcks.size(), 2u);
}

// The cabled kMatchCollection path, kept until the PDN sends record frames.
inline void
fdnStoresACabledMatchCollectionFrameWithItsSenderAndNoRecordKey(FdnRecordStoreTests* suite) {
    const Match match("0badc0de-aaaa-4000-8000-bbbbbbbbbbbb");
    MatchRecordPacket packet{};
    packet.matchSeq = 6;
    match.serialize(packet.matchData);

    suite->fdnDevice.mockPeerComms->triggerPacketCallback(suite->pdnMac, PktType::kMatchCollection,
                                                          reinterpret_cast<const uint8_t*>(&packet),
                                                          sizeof(packet));
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u)
    << "the cabled path took a frame from a PDN that is not Connected";
    EXPECT_EQ(suite->collectionAcksSent, 0u);

    suite->fdnRdc.setJackStatus(SerialIdentifier::INPUT_JACK, JackStatus::CONNECTED);
    suite->fdnRdc.setPeerDeviceType(SerialIdentifier::INPUT_JACK, DeviceType::PDN);
    suite->fdnRdc.setPeerMac(SerialIdentifier::INPUT_JACK, suite->pdnMac);
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(suite->pdnMac, PktType::kMatchCollection,
                                                          reinterpret_cast<const uint8_t*>(&packet),
                                                          sizeof(packet));
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    EXPECT_EQ(suite->collectionAcksSent, 1u);
    EXPECT_TRUE(suite->sentAcks.empty()) << "the cabled path answers on its own packet type";

    // What it stored is the record frame this PDN would send with no record key:
    // same sender, number and bytes, and a zero trailer.
    MatchRecordFramePacket sameAsRecordFrame = FdnRecordStoreTests::matchFrame(6, 0);
    memcpy(sameAsRecordFrame.recordBytes, packet.matchData, sizeof(packet.matchData));
    EXPECT_TRUE(suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->pdnMac,
                                          sameAsRecordFrame));
    suite->deliver(PktType::kMatchRecord, suite->otherPdnMac, sameAsRecordFrame);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u)
    << "the cabled record was not stored under its sender's MAC";
}
