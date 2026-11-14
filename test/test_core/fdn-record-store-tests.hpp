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
 * storage, and a radio that records every stored acknowledgement it is given.
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

        fdnRecordStore = new FdnRecordStore(fdnDevice.wirelessManager, &fdnPrefs);
    }

    /** Releases the store before the radio it is registered on. */
    void TearDown() override {
        delete fdnRecordStore;
        fdnRecordStore = nullptr;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /** Replaces the store with a fresh one on the same storage, as a reboot does. */
    void restartStore() {
        delete fdnRecordStore;
        fdnRecordStore = nullptr;
        fdnRecordStore = new FdnRecordStore(fdnDevice.wirelessManager, &fdnPrefs);
    }

    /**
     * Drops the unreadable head and retires the readable match batch, as an
     * upload of it that the server answered does.
     */
    bool retireMatchBatch() {
        fdnRecordStore->dropUnreadableOldest(RecordKind::MATCH);
        uint32_t position = 0;
        const size_t batchCount = fdnRecordStore->countReadableMatchBatch(position);
        if (batchCount == 0) return false;
        fdnRecordStore->retireMatchBatch(position, batchCount);
        return true;
    }

    /** Reads the oldest crash record and retires it, as an upload the server answered does. */
    bool retireOldestCrash() {
        FdnStoredCrashEntry entry{};
        uint32_t position = 0;
        if (!fdnRecordStore->readOldestCrash(entry, position)) return false;
        fdnRecordStore->retireCrash(position);
        return true;
    }

    /** A crash record frame whose record bytes are all @p fill. */
    static CrashRecordFramePacket crashFrame(uint32_t recordNumber, uint8_t fill) {
        CrashRecordFramePacket frame{};
        frame.kind = static_cast<uint8_t>(RecordKind::CRASH);
        frame.recordNumber = recordNumber;
        memset(frame.content.recordBytes, fill, sizeof(frame.content.recordBytes));
        return frame;
    }

    /** A match record frame whose record bytes are all @p fill, with an all-zero tag. */
    static MatchRecordFramePacket matchFrame(uint32_t recordNumber, uint8_t fill) {
        MatchRecordFramePacket frame{};
        frame.kind = static_cast<uint8_t>(RecordKind::MATCH);
        frame.recordNumber = recordNumber;
        memset(frame.content.recordBytes, fill, sizeof(frame.content.recordBytes));
        return frame;
    }

    /** A match record frame carrying a real match, which the upload can encode. */
    static MatchRecordFramePacket uploadableMatchFrame(uint32_t recordNumber) {
        char matchId[37];
        snprintf(matchId, sizeof(matchId), "%08u-abcd-4000-8000-0123456789ab", recordNumber);
        const Match match(matchId);
        MatchRecordFramePacket frame = matchFrame(recordNumber, 0);
        match.serialize(frame.content.recordBytes);
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

    /** The record check over what @p frame carries: record bytes, then the tag if it has one. */
    template <class Frame>
    static std::array<uint8_t, RECORD_CHECK_SIZE> recordCheckOf(const Frame& frame) {
        uint8_t recordCheck[RECORD_CHECK_SIZE] = {};
        computeRecordCheck(frame.content, recordCheck);
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
    RefusingPrefsDriver fdnPrefs{"fdn-record-prefs", {FDN_RECORD_STORE_NAMESPACE}};
    FdnRecordStore* fdnRecordStore = nullptr;
    uint8_t pdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x31};
    uint8_t otherPdnMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x32};
    std::vector<SentRecordAck> sentAcks;
    uint8_t nextSeqId = 0;

private:
    void noteSentFrame(const uint8_t* toMac, PktType type, const uint8_t* data, size_t length) {
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

/** Expects the stored @p entry to hold @p frame from @p fromMac with nothing altered. */
template <class Entry, class Frame>
void expectEntryCarriesFrame(const Entry& entry, const uint8_t* fromMac, const Frame& frame) {
    static_assert(sizeof(entry.content) == sizeof(frame.content),
                  "the entry and the frame are of different record kinds");
    EXPECT_EQ(memcmp(entry.senderMac, fromMac, sizeof(entry.senderMac)), 0)
    << "the sender's MAC address was altered";
    EXPECT_EQ(entry.kind, frame.kind);
    EXPECT_EQ(static_cast<uint32_t>(entry.recordNumber), static_cast<uint32_t>(frame.recordNumber));
    EXPECT_EQ(memcmp(&entry.content, &frame.content, sizeof(frame.content)), 0)
    << "the record bytes or, for a match record, the tag were altered";
}

inline void fdnStoresThenAcknowledgesARecordFromAPdnThatIsNotConnected(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(7, 0x5A);
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

    MatchRecordFramePacket match = FdnRecordStoreTests::matchFrame(12, 0x3C);
    memset(match.content.tag, 0xC4, sizeof(match.content.tag));
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
    const CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(4, 0x41);
    MatchRecordFramePacket match = FdnRecordStoreTests::matchFrame(9, 0x42);
    memset(match.content.tag, 0x77, sizeof(match.content.tag));
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

    const MatchRecordFramePacket uploadableMatch = FdnRecordStoreTests::uploadableMatchFrame(4);
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, uploadableMatch,
                   sizeof(MatchRecordFramePacket) - 1);

    // A crash record frame with 17 trailing bytes.
    uint8_t withTrailingBytes[65] = {};
    const CrashRecordFramePacket current = FdnRecordStoreTests::crashFrame(5, 0x34);
    memcpy(withTrailingBytes, &current, sizeof(current));
    withTrailingBytes[0] = 0xC8;
    suite->fdnDevice.mockPeerComms->triggerPacketCallback(
    suite->pdnMac, PktType::kCrashRecord, withTrailingBytes, sizeof(withTrailingBytes));

    // A match record frame without its tag.
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, uploadableMatch,
                   sizeof(MatchRecordFramePacket) - RECORD_TAG_SIZE);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 0u);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
    EXPECT_TRUE(suite->sentAcks.empty());

    // The same frame at its full length is taken, so only the length was at fault.
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, uploadableMatch);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    EXPECT_EQ(suite->sentAcks.size(), 1u);
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

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, first);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, otherBytes);
    suite->deliver(PktType::kCrashRecord, suite->otherPdnMac, first);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 3u);
    ASSERT_EQ(suite->sentAcks.size(), 3u);
    EXPECT_EQ(suite->sentAcks[0].recordCheck, FdnRecordStoreTests::recordCheckOf(first));
    EXPECT_EQ(suite->sentAcks[1].recordCheck, FdnRecordStoreTests::recordCheckOf(otherBytes));
    EXPECT_NE(suite->sentAcks[0].recordCheck, suite->sentAcks[1].recordCheck);

    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, first))
    << "the first record was displaced";
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, otherBytes));
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

inline void fdnAcknowledgesACrashRecordWithTheCheckOverItsBytesAlone(FdnRecordStoreTests* suite) {
    CrashRecordFramePacket crash = FdnRecordStoreTests::crashFrame(9, 0);
    for (size_t i = 0; i < sizeof(crash.content.recordBytes); ++i) {
        crash.content.recordBytes[i] = static_cast<uint8_t>(i);
    }
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, crash);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 1u);
    // First 8 bytes of SHA-256 over bytes 0..41 and nothing else, from Python's hashlib.
    const std::array<uint8_t, RECORD_CHECK_SIZE> expected = {0x40, 0xa1, 0xed, 0x73,
                                                             0xb4, 0x60, 0x30, 0xc8};
    EXPECT_EQ(suite->sentAcks[0].recordCheck, expected);
    EXPECT_EQ(suite->sentAcks[0].recordNumber, 9u);

    suite->restartStore();
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kCrashRecord, RecordKind::CRASH, suite->pdnMac, crash));
}

// The FDN does not read the tag: a match record whose tag is 16 zero bytes is
// stored and acknowledged like any other.
inline void fdnStoresAndAcknowledgesAMatchRecordWithAnAllZeroTag(FdnRecordStoreTests* suite) {
    MatchRecordFramePacket match = FdnRecordStoreTests::matchFrame(9, 0);
    for (size_t i = 0; i < sizeof(match.content.recordBytes); ++i) {
        match.content.recordBytes[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, match);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 1u);
    // First 8 bytes of SHA-256 over those record bytes then 16 zero bytes, from Python's hashlib.
    const std::array<uint8_t, RECORD_CHECK_SIZE> expected = {0xf9, 0xb3, 0xe7, 0xa3,
                                                             0xbd, 0xb5, 0x64, 0xa7};
    EXPECT_EQ(suite->sentAcks[0].recordCheck, expected);
    EXPECT_EQ(suite->sentAcks[0].type, PktType::kMatchRecordAck);
    EXPECT_EQ(suite->sentAcks[0].recordNumber, 9u);

    // What was kept is the zero tag: the same record and number under a tag is another record.
    suite->restartStore();
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->pdnMac, match));
    MatchRecordFramePacket tagged = match;
    memset(tagged.content.tag, 0x99, sizeof(tagged.content.tag));
    const size_t acksBefore = suite->sentAcks.size();
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, tagged);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u)
    << "a record that differs only in its tag was taken for a duplicate";
    ASSERT_EQ(suite->sentAcks.size(), acksBefore + 1);
    EXPECT_EQ(suite->sentAcks.back().recordCheck, FdnRecordStoreTests::recordCheckOf(tagged));
    EXPECT_NE(suite->sentAcks.back().recordCheck, expected);

    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->pdnMac, match))
    << "the record with the all-zero tag was displaced";
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->pdnMac, tagged));
}

// The tag here is arbitrary bytes, so it cannot match the record; the FDN has
// no key to find that out with and must keep it as it came.
inline void fdnStoresARecordWithABadTagUnchanged(FdnRecordStoreTests* suite) {
    MatchRecordFramePacket match = FdnRecordStoreTests::matchFrame(4, 0);
    for (size_t i = 0; i < sizeof(match.content.recordBytes); ++i) {
        match.content.recordBytes[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    for (size_t i = 0; i < sizeof(match.content.tag); ++i) {
        match.content.tag[i] = static_cast<uint8_t>(0xA0 + i);
    }
    suite->deliver(PktType::kMatchRecord, suite->pdnMac, match);

    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u);
    ASSERT_EQ(suite->sentAcks.size(), 1u);
    // First 8 bytes of SHA-256 over those record bytes then that tag, from Python's hashlib.
    const std::array<uint8_t, RECORD_CHECK_SIZE> expected = {0xb1, 0xf3, 0x8a, 0x53,
                                                             0x47, 0xc2, 0xf8, 0xd0};
    EXPECT_EQ(suite->sentAcks[0].recordCheck, expected);
    EXPECT_EQ(suite->sentAcks[0].type, PktType::kMatchRecordAck);

    // Read back from storage, the record still matches the frame byte for byte,
    // and one differing tag byte makes a different record.
    suite->restartStore();
    EXPECT_TRUE(
    suite->isRecognisedAsHeld(PktType::kMatchRecord, RecordKind::MATCH, suite->pdnMac, match))
    << "the record or its tag was altered on the way into storage";
    MatchRecordFramePacket otherTag = match;
    otherTag.content.tag[RECORD_TAG_SIZE - 1] ^= 0x01;
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

    ASSERT_TRUE(suite->retireMatchBatch());
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

inline void fdnErasesARetiredBatchEvenWhenTheHeadCannotAdvance(FdnRecordStoreTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(2));
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);

    suite->fdnPrefs.refusedCounterKey = "m_head";
    ASSERT_TRUE(suite->retireMatchBatch());
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
    // slots; dropping the unreadable head clears them instead of stalling behind them.
    suite->fdnPrefs.refusedCounterKey.clear();
    suite->restartStore();
    ASSERT_TRUE(suite->retireMatchBatch());
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
    EXPECT_FALSE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "m_2"));
}

// The rings share one partition, so room one of them frees is room for the other.
inline void fdnRetiredMatchBatchThatFreesSlotsRestoresCrashIntake(FdnRecordStoreTests* suite) {
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

    ASSERT_TRUE(suite->retireMatchBatch());

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

inline void
fdnWithFullMatchStorageStillAcknowledgesAResendAndKnowsItAfterARestart(FdnRecordStoreTests* suite) {
    // From a PDN that is not Connected on any of the FDN's jacks: intake has no cable check.
    for (uint32_t number = 1; number <= 65; ++number) {
        suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                       FdnRecordStoreTests::uploadableMatchFrame(number));
    }
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 64u);
    EXPECT_EQ(suite->sentAcks.size(), 64u)
    << "65th match must not be ACKed while the store is full";

    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(64));
    EXPECT_EQ(suite->sentAcks.size(), 65u)
    << "a resend after a lost ack is re-acked even when full";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 64u);

    suite->restartStore();
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    EXPECT_EQ(suite->sentAcks.size(), 66u) << "stored records are known again after a reboot";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 64u);
}

// The entry read here is what the match upload is handed, so the sender, the
// record bytes and a tag the FDN cannot check must come back as they arrived.
inline void fdnDropsAnUnreadableHeadInsteadOfFailingEveryBatch(FdnRecordStoreTests* suite) {
    suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                   FdnRecordStoreTests::uploadableMatchFrame(1));
    MatchRecordFramePacket tagged = FdnRecordStoreTests::uploadableMatchFrame(2);
    for (size_t i = 0; i < sizeof(tagged.content.tag); ++i) {
        tagged.content.tag[i] = static_cast<uint8_t>(0xA0 + i);
    }
    suite->deliver(PktType::kMatchRecord, suite->otherPdnMac, tagged);
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);
    const uint8_t torn[3] = {1, 2, 3};
    suite->fdnPrefs.writeBytes(FDN_RECORD_STORE_NAMESPACE, "m_0", torn, sizeof(torn));

    uint32_t position = 0;
    EXPECT_EQ(suite->fdnRecordStore->countReadableMatchBatch(position), 0u)
    << "counting the batch dropped a record; only the explicit command may";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);

    suite->fdnRecordStore->dropUnreadableOldest(RecordKind::MATCH);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u)
    << "the torn head still counts as held";
    EXPECT_EQ(suite->fdnRecordStore->countReadableMatchBatch(position), 1u)
    << "the readable record behind a torn head is still a batch";
    FdnStoredMatchEntry entry{};
    ASSERT_TRUE(suite->fdnRecordStore->readMatch(position, entry));
    expectEntryCarriesFrame(entry, suite->otherPdnMac, tagged);
}

inline void fdnMatchBatchStopsShortOfAnUnreadableRecord(FdnRecordStoreTests* suite) {
    for (uint32_t number = 1; number <= 3; ++number) {
        suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                       FdnRecordStoreTests::uploadableMatchFrame(number));
    }
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 3u);
    const uint8_t torn[3] = {1, 2, 3};
    suite->fdnPrefs.writeBytes(FDN_RECORD_STORE_NAMESPACE, "m_1", torn, sizeof(torn));

    FdnStoredMatchEntry entry{};
    uint32_t position = 0;
    ASSERT_EQ(suite->fdnRecordStore->countReadableMatchBatch(position), 1u)
    << "a torn record further in must not fail the batch ahead of it";
    ASSERT_TRUE(suite->fdnRecordStore->readMatch(position, entry));
    expectEntryCarriesFrame(entry, suite->pdnMac, FdnRecordStoreTests::uploadableMatchFrame(1));
    EXPECT_FALSE(suite->fdnRecordStore->readMatch(position + 1, entry))
    << "the torn record read as a match";
    suite->fdnRecordStore->retireMatchBatch(position, 1);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);

    // The batch that was retired is no longer the oldest records held.
    suite->fdnRecordStore->retireMatchBatch(position, 1);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u)
    << "a batch was retired twice, which retires records that were never uploaded";

    suite->fdnRecordStore->dropUnreadableOldest(RecordKind::MATCH);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 1u)
    << "the torn record is dropped once it is the oldest";
    ASSERT_EQ(suite->fdnRecordStore->countReadableMatchBatch(position), 1u);
    ASSERT_TRUE(suite->fdnRecordStore->readMatch(position, entry));
    expectEntryCarriesFrame(entry, suite->pdnMac, FdnRecordStoreTests::uploadableMatchFrame(3));
    EXPECT_FALSE(suite->fdnRecordStore->readMatch(position + 1, entry))
    << "a place past the newest stored match read as a record";
    suite->fdnRecordStore->retireMatchBatch(position, 1);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
}

// ---- Relayed crash records, read and retired one at a time ----

inline void fdnHandsOutCrashRecordsOldestFirstAsTheyWereStored(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket first = FdnRecordStoreTests::crashFrame(7, 0x5A);
    const CrashRecordFramePacket second = FdnRecordStoreTests::crashFrame(2, 0x5B);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, first);
    suite->deliver(PktType::kCrashRecord, suite->otherPdnMac, second);

    FdnStoredCrashEntry entry{};
    uint32_t firstPosition = 0;
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, firstPosition));
    expectEntryCarriesFrame(entry, suite->pdnMac, first);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u)
    << "reading a record must not retire it";

    suite->fdnRecordStore->retireCrash(firstPosition);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    uint32_t secondPosition = 0;
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, secondPosition));
    expectEntryCarriesFrame(entry, suite->otherPdnMac, second);

    // The record read first is gone, so retiring it again must not take the next one.
    suite->fdnRecordStore->retireCrash(firstPosition);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u)
    << "a record was retired on the answer for another";

    suite->fdnRecordStore->retireCrash(secondPosition);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 0u);
    EXPECT_FALSE(suite->fdnRecordStore->readOldestCrash(entry, secondPosition));

    // With nothing held there is nothing to retire.
    suite->fdnRecordStore->retireCrash(secondPosition);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 0u);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, first);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, secondPosition));
    expectEntryCarriesFrame(entry, suite->pdnMac, first);
}

// Any freed slot lets storage be asked again, one retired crash record included.
inline void fdnRetiringOneCrashRecordFreesItsSlotAndClearsTheRefusal(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket first = FdnRecordStoreTests::crashFrame(1, 0x10);
    const CrashRecordFramePacket third = FdnRecordStoreTests::crashFrame(3, 0x12);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, first);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(2, 0x11));
    ASSERT_EQ(suite->sentAcks.size(), 2u);

    suite->fdnPrefs.refuseBlobWrites = true;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, third);
    ASSERT_EQ(suite->sentAcks.size(), 2u) << "a record the storage refused was acknowledged";
    suite->fdnPrefs.refuseBlobWrites = false;
    const size_t writesAfterRefusal = suite->fdnPrefs.writes;
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, third);
    ASSERT_EQ(suite->fdnPrefs.writes, writesAfterRefusal)
    << "storage that refused a record was asked again before anything was freed";

    ASSERT_TRUE(suite->retireOldestCrash());
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
    EXPECT_FALSE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "c_0"))
    << "a retired record stayed in storage";
    EXPECT_TRUE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "c_1"))
    << "retiring one record erased another";

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, third);
    EXPECT_EQ(suite->sentAcks.size(), 3u)
    << "the slot one retired crash record freed did not let the store take records again";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u);

    // The retired record is no longer held, so the same frame is a new record.
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, first);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 3u);
    EXPECT_EQ(suite->sentAcks.size(), 4u);
}

inline void fdnWithFullCrashStorageTakesOneMoreAfterOneIsRetired(FdnRecordStoreTests* suite) {
    for (uint32_t number = 1; number <= FDN_CRASH_RECORD_CAPACITY; ++number) {
        suite->deliver(PktType::kCrashRecord, suite->pdnMac,
                       FdnRecordStoreTests::crashFrame(number, static_cast<uint8_t>(number)));
    }
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), FDN_CRASH_RECORD_CAPACITY);
    const CrashRecordFramePacket oneMore = FdnRecordStoreTests::crashFrame(33, 0x99);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, oneMore);
    ASSERT_EQ(suite->sentAcks.size(), FDN_CRASH_RECORD_CAPACITY);

    ASSERT_TRUE(suite->retireOldestCrash());
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH),
              FDN_CRASH_RECORD_CAPACITY - 1);

    suite->deliver(PktType::kCrashRecord, suite->pdnMac, oneMore);
    EXPECT_EQ(suite->sentAcks.size(), FDN_CRASH_RECORD_CAPACITY + 1)
    << "the retired record's slot was not free for a new record";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), FDN_CRASH_RECORD_CAPACITY);

    // The new record took the freed slot without disturbing the oldest one left.
    FdnStoredCrashEntry entry{};
    uint32_t position = 0;
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, position));
    expectEntryCarriesFrame(entry, suite->pdnMac, FdnRecordStoreTests::crashFrame(2, 2));
}

inline void fdnErasesARetiredCrashRecordEvenWhenTheHeadCannotAdvance(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket second = FdnRecordStoreTests::crashFrame(2, 0x11);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(1, 0x10));
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, second);

    suite->fdnPrefs.refusedCounterKey = "c_head";
    ASSERT_TRUE(suite->retireOldestCrash());
    EXPECT_FALSE(suite->fdnPrefs.holdsBlob(FDN_RECORD_STORE_NAMESPACE, "c_0"))
    << "a retired record stayed in storage";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u)
    << "an erased record still counts as held";

    // A restart reads the head that was never stored, which rests on the erased
    // slot; dropping the unreadable head clears it instead of stalling behind it.
    suite->fdnPrefs.refusedCounterKey.clear();
    suite->restartStore();
    FdnStoredCrashEntry entry{};
    uint32_t position = 0;
    EXPECT_FALSE(suite->fdnRecordStore->readOldestCrash(entry, position));
    suite->fdnRecordStore->dropUnreadableOldest(RecordKind::CRASH);
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, position));
    expectEntryCarriesFrame(entry, suite->pdnMac, second);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u);
}

// ---- A retire names what was read, and is refused once that is no longer the oldest ----

// An answer for a record that has already been retired must not retire or
// bring back anything: the position it names is not the oldest held any more.
inline void fdnRefusesToRetireACrashRecordThatIsNoLongerTheOldest(FdnRecordStoreTests* suite) {
    const CrashRecordFramePacket second = FdnRecordStoreTests::crashFrame(2, 0x21);
    const CrashRecordFramePacket third = FdnRecordStoreTests::crashFrame(3, 0x22);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, FdnRecordStoreTests::crashFrame(1, 0x20));
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, second);
    suite->deliver(PktType::kCrashRecord, suite->pdnMac, third);

    FdnStoredCrashEntry entry{};
    uint32_t firstPosition = 0;
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, firstPosition));
    EXPECT_TRUE(suite->fdnRecordStore->retireCrash(firstPosition));
    EXPECT_FALSE(suite->fdnRecordStore->retireCrash(firstPosition))
    << "a record that was already retired was reported as retired again";
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 2u);

    uint32_t secondPosition = 0;
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, secondPosition));
    expectEntryCarriesFrame(entry, suite->pdnMac, second);
    EXPECT_TRUE(suite->fdnRecordStore->retireCrash(secondPosition));

    // Two places behind the oldest: taking it back would move the store's
    // head backwards over slots that are already erased.
    EXPECT_FALSE(suite->fdnRecordStore->retireCrash(firstPosition));
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::CRASH), 1u)
    << "a stale retire changed how many records are held";
    uint32_t thirdPosition = 0;
    ASSERT_TRUE(suite->fdnRecordStore->readOldestCrash(entry, thirdPosition))
    << "a stale retire left the oldest record unreadable";
    expectEntryCarriesFrame(entry, suite->pdnMac, third);
}

// The same for a match batch: here the stale batch reaches past the oldest
// record into one that was never part of an answered upload.
inline void fdnRefusesToRetireAMatchBatchThatIsNoLongerTheOldest(FdnRecordStoreTests* suite) {
    for (uint32_t number = 1; number <= 3; ++number) {
        suite->deliver(PktType::kMatchRecord, suite->pdnMac,
                       FdnRecordStoreTests::uploadableMatchFrame(number));
    }
    uint32_t position = 0;
    ASSERT_EQ(suite->fdnRecordStore->countReadableMatchBatch(position), 3u);
    suite->fdnRecordStore->retireMatchBatch(position, 1);
    ASSERT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u);

    suite->fdnRecordStore->retireMatchBatch(position, 2);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 2u)
    << "a batch counted from a position that is no longer the oldest was retired";
    FdnStoredMatchEntry entry{};
    ASSERT_TRUE(suite->fdnRecordStore->readMatch(position + 1, entry))
    << "a stale retire erased a record that was never uploaded";
    expectEntryCarriesFrame(entry, suite->pdnMac, FdnRecordStoreTests::uploadableMatchFrame(2));
    ASSERT_TRUE(suite->fdnRecordStore->readMatch(position + 2, entry));
    expectEntryCarriesFrame(entry, suite->pdnMac, FdnRecordStoreTests::uploadableMatchFrame(3));

    // The batch counted from the oldest record is still taken back.
    uint32_t currentPosition = 0;
    ASSERT_EQ(suite->fdnRecordStore->countReadableMatchBatch(currentPosition), 2u);
    suite->fdnRecordStore->retireMatchBatch(currentPosition, 2);
    EXPECT_EQ(suite->fdnRecordStore->getStoredCount(RecordKind::MATCH), 0u);
}
