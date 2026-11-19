#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include "alleycat-server/crash-api.hpp"
#include "alleycat-server/fdn-record-store.hpp"
#include "alleycat-server/matches-api.hpp"
#include "device/crash/crash-record.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "game/match.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/wireless-types.hpp"

inline constexpr unsigned long FDN_UPLOAD_INTERVAL_MS = 300000;
// With no AP in range a queued request never answers; the cycle gives the radio back.
inline constexpr unsigned long FDN_UPLOAD_CYCLE_CAP_MS = 15000;
// alleycat-rs answers a batch naming a match it already holds under another
// event with 409 and this text; the server has that match, so the copy can go.
inline constexpr const char FDN_CROSS_EVENT_409_TEXT[] = "already belongs to another event";
static_assert(FDN_MATCH_RECORD_CAPACITY <= QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH,
              "a full match ring goes up in one batch");

// Uploads the FDN's stored records: one cycle at boot, then one every
// FDN_UPLOAD_INTERVAL_MS while records wait. A cycle owns Wi-Fi until it ends
// or hits FDN_UPLOAD_CYCLE_CAP_MS, then hands the radio back to ESP-NOW. It
// sends the match batch first, then crash records oldest first, one at a time.
class FdnRecordUploader {
public:
    /**
     * @p isCabled answers whether any jack has a peer; Wi-Fi turns ESP-NOW
     * off, so no cycle starts while it is true.
     */
    FdnRecordUploader(WirelessManager* wirelessManager, FdnRecordStore* fdnRecordStore,
                      std::function<bool()> isCabled)
        : wirelessManager(wirelessManager)
        , fdnRecordStore(fdnRecordStore)
        , isCabled(std::move(isCabled)) {}

    /** Call every tick: starts a due cycle, acts on an answer, ends the cycle. */
    void sync() {
        if (!cycleTimer.isRunning()) {
            startCycleIfDue();
            return;
        }
        if (outcome == Outcome::WAITING) {
            if (cycleTimer.expired()) {
                LOG_W("FDN_UPLOAD", "Upload cycle hit its cap; records stay for the next cycle");
                endCycle();
            }
            return;
        }
        const Outcome answered = outcome;
        outcome = Outcome::NONE;
        if (answered == Outcome::KEEP) {
            // The server answered the matches (409: no active event); crash records don't depend on
            // it.
            if (sentKind != RecordKind::MATCH || answeredStatus == 0) {
                endCycle();
                return;
            }
            matchesHeld = true;
        } else if (answered == Outcome::RETIRE) {
            retireSent();
        } else if (sentCount == 1) {
            LOG_E("FDN_UPLOAD", "Server refused record %u alone (HTTP %d); retiring it",
                  static_cast<unsigned>(sentNumber), answeredStatus);
            retireSent();
        } else {
            // A batch the server refused whole: find the bad record one at a time.
            probing = true;
        }
        sendNext();
    }

private:
    enum class Outcome { NONE, WAITING, RETIRE, REFUSED, KEEP };

    void startCycleIfDue() {
        if (intervalTimer.isRunning() && !intervalTimer.expired()) return;
        // Checked each tick until the cable comes out; the cycle stays due.
        if (isCabled()) return;
        matchesHeld = false;
        cycleTimer.setTimer(FDN_UPLOAD_CYCLE_CAP_MS);
        sendNext();
    }

    void sendNext() {
        ++generation;
        outcome = Outcome::WAITING;
        const size_t limit = probing ? 1 : FDN_MATCH_RECORD_CAPACITY;
        sentCount =
        matchesHeld ? 0 : fdnRecordStore->getMatchRecords().peekOldest(matchBatch, limit);
        if (sentCount > 0) {
            sentKind = RecordKind::MATCH;
            sentNumber = matchBatch[0].recordNumber;
            MatchesApi::uploadQuickdrawMatches(
            wirelessManager, sentCount,
            [this](size_t index, MatchRecord& record) {
                memcpy(&record, matchBatch[index].content, sizeof(record));
                return true;
            },
            [](const QuickdrawMatchRejection& rejection) {
                char matchId[UUIDBytes::TEXT_BUFFER_SIZE];
                rejection.matchId.format(matchId);
                LOG_W("FDN_UPLOAD", "Server rejected match %u (%s): code %d; retiring it",
                      static_cast<unsigned>(rejection.matchIndex), matchId,
                      static_cast<int>(rejection.rawCode));
            },
            [this, cycle = generation](const QuickdrawUploadResult&) {
                answer(cycle, Outcome::RETIRE, 0);
            },
            [this, cycle = generation](const WirelessErrorInfo& error) {
                onFailure(cycle, error);
            });
            return;
        }
        FdnRecordStore::CrashRing::Entry crash{};
        sentCount = fdnRecordStore->getCrashRecords().peekOldest(&crash, 1);
        if (sentCount == 0) {
            endCycle();
            return;
        }
        sentKind = RecordKind::CRASH;
        sentNumber = crash.recordNumber;
        CrashPacket packet{};
        memcpy(&packet, crash.content, sizeof(packet));
        CrashApi::uploadCrash(
        wirelessManager, fromCrashPacket(packet), crash.senderMac,
        [this, cycle = generation](const std::string&) { answer(cycle, Outcome::RETIRE, 0); },
        [this, cycle = generation](const WirelessErrorInfo& error) { onFailure(cycle, error); });
    }

    void onFailure(uint32_t cycle, const WirelessErrorInfo& error) {
        LOG_W("FDN_UPLOAD", "Upload failed: %s", error.message.c_str());
        // A 409 is batch-wide "no active event" unless its text names a match
        // the server already holds under another event: that copy is refused.
        const bool refused =
        error.httpStatus == 400 || error.httpStatus == 413 || error.httpStatus == 415 ||
        (error.httpStatus == 409 &&
         error.responseBody.find(FDN_CROSS_EVENT_409_TEXT) != std::string::npos);
        answer(cycle, refused ? Outcome::REFUSED : Outcome::KEEP, error.httpStatus);
    }

    // Records and returns: sync() acts on it, outside the HTTP client's call.
    void answer(uint32_t cycle, Outcome result, int httpStatus) {
        // An answer after the cap or for an earlier request is stale.
        if (cycle != generation || outcome != Outcome::WAITING) return;
        outcome = result;
        answeredStatus = httpStatus;
    }

    void retireSent() {
        if (sentKind == RecordKind::MATCH) {
            fdnRecordStore->getMatchRecords().retireOldest(sentCount);
            probing = false;
        } else {
            fdnRecordStore->getCrashRecords().retireOldest(sentCount);
        }
    }

    void endCycle() {
        ++generation;
        outcome = Outcome::NONE;
        cycleTimer.invalidate();
        intervalTimer.setTimer(FDN_UPLOAD_INTERVAL_MS);
        wirelessManager->enablePeerCommsMode();
    }

    WirelessManager* wirelessManager = nullptr;
    FdnRecordStore* fdnRecordStore = nullptr;
    std::function<bool()> isCabled;
    SimpleTimer cycleTimer;
    SimpleTimer intervalTimer;
    // Set by a refused batch: send only the oldest match until one retires.
    bool probing = false;
    // Set when the server kept the matches this cycle; the crash records still go.
    bool matchesHeld = false;
    Outcome outcome = Outcome::NONE;
    uint32_t generation = 0;
    RecordKind sentKind = RecordKind::MATCH;
    size_t sentCount = 0;
    uint32_t sentNumber = 0;
    int answeredStatus = 0;
    FdnRecordStore::MatchRing::Entry matchBatch[FDN_MATCH_RECORD_CAPACITY] = {};
};
