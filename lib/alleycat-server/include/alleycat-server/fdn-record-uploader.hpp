#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include "alleycat-server/crash-api.hpp"
#include "alleycat-server/fdn-record-store.hpp"
#include "alleycat-server/health-api.hpp"
#include "alleycat-server/matches-api.hpp"
#include "device/crash/crash-record.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "game/match.hpp"
#include "utils/http-date.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/wireless-types.hpp"

inline constexpr unsigned long FDN_UPLOAD_INTERVAL_MS = 300000;
// With no AP in range a queued request never answers; the cycle gives the radio back.
inline constexpr unsigned long FDN_UPLOAD_CYCLE_CAP_MS = 15000;
// The shortest gap between cycles when a full match store starts one early.
inline constexpr unsigned long FDN_UPLOAD_FULL_STORE_REST_MS = 30000;
// alleycat-rs answers a batch naming a match it already holds under another
// event with 409 and this text; the server has that match, so the copy can go.
inline constexpr const char FDN_CROSS_EVENT_409_TEXT[] = "already belongs to another event";
// Names the server's active event; a change there releases held matches.
inline constexpr const char FDN_GAME_STATE_ENDPOINT[] = "/game-state";
// A match held for another event is also retried this long after it was held.
inline constexpr unsigned long FDN_HELD_MATCH_RETRY_MS = 1800000;
static_assert(FDN_MATCH_RECORD_CAPACITY <= QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH,
              "a full match ring goes up in one batch");

/** The active event's id in a GET /game-state body, or "" when none is selected. */
inline std::string activeEventIdFrom(const std::string& body) {
    static constexpr char KEY[] = "\"active_event\"";
    size_t at = body.find(KEY);
    if (at == std::string::npos) return "";
    at = body.find_first_not_of(": ", at + sizeof(KEY) - 1);
    if (at == std::string::npos || body[at] != '{') return "";
    const size_t id = body.find("\"id\":\"", at);
    if (id == std::string::npos) return "";
    const size_t end = body.find('"', id + 6);
    return end == std::string::npos ? "" : body.substr(id + 6, end - id - 6);
}

// Uploads the FDN's stored records: one cycle at boot, then one every
// FDN_UPLOAD_INTERVAL_MS while records wait, or sooner once the match store is
// full. A cycle owns Wi-Fi until it ends
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
                if (sentKind == RecordKind::MATCH) matchesFailed = true;
                endCycle();
            }
            return;
        }
        const Outcome answered = outcome;
        outcome = Outcome::NONE;
        if (askingServer) {
            askingServer = false;
            sendNext();
            return;
        }
        if (answered == Outcome::KEEP) {
            if (sentKind == RecordKind::MATCH) matchesFailed = true;
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
        if (intervalTimer.isRunning() && !intervalTimer.expired() && !isFullAndRested()) return;
        // Checked each tick until the cable comes out; the cycle stays due.
        if (isCabled()) return;
        matchesHeld = false;
        matchesFailed = false;
        timeAsked = false;
        eventAsked = false;
        cycleTimer.setTimer(FDN_UPLOAD_CYCLE_CAP_MS);
        sendNext();
    }

    // A full match store refuses relayed matches, so it goes up early, but no
    // sooner than FDN_UPLOAD_FULL_STORE_REST_MS after the last cycle ended, and
    // not after a cycle whose matches failed: a failing server waits its interval.
    // A store full of held matches has nothing to send early.
    bool isFullAndRested() {
        const size_t stored = fdnRecordStore->getMatchRecords().getStoredCount();
        return !matchesFailed && stored >= FDN_MATCH_RECORD_CAPACITY &&
               heldMatches.size() < stored && (!restTimer.isRunning() || restTimer.expired());
    }

    void sendNext() {
        ++generation;
        outcome = Outcome::WAITING;
        if (!timeAsked && !isClockSet()) {
            askTime();
            return;
        }
        if (!heldMatches.empty() && !eventAsked) {
            askEvent();
            return;
        }
        if (heldRetryTimer.expired()) releaseHeld = true;
        if (releaseHeld) {
            LOG_I("FDN_UPLOAD", "Retrying %u matches held for another event",
                  static_cast<unsigned>(heldMatches.size()));
            heldMatches.clear();
            heldEvent.clear();
            heldRetryTimer.invalidate();
            releaseHeld = false;
        }
        const size_t limit = probing ? 1 : FDN_MATCH_RECORD_CAPACITY;
        // A match goes up only with the FDN's clock set (#518).
        sentCount = matchesHeld || !isClockSet() ? 0 : peekUnheldMatches(limit);
        if (sentCount > 0) {
            sentKind = RecordKind::MATCH;
            sentNumber = matchBatch[0].recordNumber;
            eventRejected.clear();
            MatchesApi::uploadQuickdrawMatches(
            wirelessManager, sentCount,
            [this](size_t index, MatchRecord& record) {
                memcpy(&record, matchBatch[index].content, sizeof(record));
                return true;
            },
            [this](const QuickdrawMatchRejection& rejection) {
                char matchId[UUIDBytes::TEXT_BUFFER_SIZE];
                rejection.matchId.format(matchId);
                const bool otherEvent =
                rejection.code == QuickdrawMatchErrorCode::INVALID_EVENT_REFERENCE;
                LOG_W("FDN_UPLOAD", "Server rejected match %u (%s): code %d; %s",
                      static_cast<unsigned>(rejection.matchIndex), matchId,
                      static_cast<int>(rejection.rawCode),
                      otherEvent ? "holding it for its event" : "retiring it");
                if (otherEvent && rejection.matchIndex < sentCount) {
                    eventRejected.push_back(rejection.matchIndex);
                }
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

    static bool isClockSet() {
        PlatformClock* clock = SimpleTimer::getPlatformClock();
        return clock != nullptr && isValidUtcUnixMs(clock->unixMilliseconds());
    }

    // Once a cycle, without the time: the answer's Date header sets the clock.
    void askTime() {
        timeAsked = true;
        askingServer = true;
        HealthApi::healthCheck(
        wirelessManager,
        [this, cycle = generation](const std::string&) { answer(cycle, Outcome::RETIRE, 0); },
        [this, cycle = generation](const WirelessErrorInfo& error) {
            answer(cycle, Outcome::KEEP, error.httpStatus);
        });
    }

    // While matches are held: the active event's id, which releases them when it changes.
    void askEvent() {
        eventAsked = true;
        askingServer = true;
        wirelessManager->queueHttpRequest(HttpRequest(
        FDN_GAME_STATE_ENDPOINT, "GET", "", "",
        [this, cycle = generation](const std::string& body) {
            if (cycle == generation) noteActiveEvent(activeEventIdFrom(body));
            answer(cycle, Outcome::RETIRE, 0);
        },
        [this, cycle = generation](const WirelessErrorInfo& error) {
            answer(cycle, Outcome::KEEP, error.httpStatus);
        }));
    }

    void noteActiveEvent(const std::string& eventId) {
        if (eventId.empty()) return;
        if (heldEvent.empty()) {
            heldEvent = eventId;
        } else if (eventId != heldEvent) {
            releaseHeld = true;
        }
    }

    bool isHeld(const FdnRecordStore::MatchRing::Entry& entry) const {
        for (const HeldMatch& held : heldMatches) {
            if (held.recordNumber == entry.recordNumber &&
                memcmp(held.senderMac, entry.senderMac, 6) == 0) {
                return true;
            }
        }
        return false;
    }

    // Peeks the oldest matches up to the first held one. Held matches at the
    // front move to the back first, so they never block the ones behind them.
    size_t peekUnheldMatches(size_t limit) {
        FdnRecordStore::MatchRing& ring = fdnRecordStore->getMatchRecords();
        const size_t stored = ring.peekOldest(matchBatch, FDN_MATCH_RECORD_CAPACITY);
        size_t unheld = 0;
        for (size_t index = 0; index < stored; ++index) {
            if (!isHeld(matchBatch[index])) ++unheld;
        }
        if (unheld == 0) return 0;
        for (size_t moved = 0; moved < stored; ++moved) {
            FdnRecordStore::MatchRing::Entry front{};
            if (ring.peekOldest(&front, 1) == 0 || !isHeld(front)) break;
            ring.retireOldest(1);
            ring.store(front);
        }
        size_t count = ring.peekOldest(matchBatch, limit);
        for (size_t index = 0; index < count; ++index) {
            if (isHeld(matchBatch[index])) count = index;
        }
        return count;
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
            FdnRecordStore::MatchRing& ring = fdnRecordStore->getMatchRecords();
            ring.retireOldest(sentCount);
            probing = false;
            // Back at the tail: the server keeps no result for these (owner ruling, RELAY-2).
            // The batch just retired left room for each.
            for (size_t index : eventRejected) {
                if (!ring.store(matchBatch[index])) {
                    LOG_E("FDN_UPLOAD", "Could not store match %u to hold it for its event",
                          static_cast<unsigned>(matchBatch[index].recordNumber));
                    continue;
                }
                HeldMatch held{};
                memcpy(held.senderMac, matchBatch[index].senderMac, 6);
                held.recordNumber = matchBatch[index].recordNumber;
                heldMatches.push_back(held);
                if (!heldRetryTimer.isRunning()) heldRetryTimer.setTimer(FDN_HELD_MATCH_RETRY_MS);
            }
            eventRejected.clear();
        } else {
            fdnRecordStore->getCrashRecords().retireOldest(sentCount);
        }
    }

    void endCycle() {
        ++generation;
        outcome = Outcome::NONE;
        askingServer = false;
        cycleTimer.invalidate();
        intervalTimer.setTimer(FDN_UPLOAD_INTERVAL_MS);
        restTimer.setTimer(FDN_UPLOAD_FULL_STORE_REST_MS);
        wirelessManager->enablePeerCommsMode();
    }

    WirelessManager* wirelessManager = nullptr;
    FdnRecordStore* fdnRecordStore = nullptr;
    std::function<bool()> isCabled;
    SimpleTimer cycleTimer;
    SimpleTimer intervalTimer;
    SimpleTimer restTimer;
    // Set by a refused batch: send only the oldest match until one retires.
    bool probing = false;
    // Set when the server kept the matches this cycle; the crash records still go.
    bool matchesHeld = false;
    // Set when the last cycle's matches failed (kept, or the cap hit while sending them).
    bool matchesFailed = false;
    // Set while a time or event check is the request out; each runs once a cycle.
    bool askingServer = false;
    bool timeAsked = false;
    bool eventAsked = false;
    // Matches the server rejected for another event, kept in the ring (RELAY-2).
    struct HeldMatch {
        uint8_t senderMac[6];
        uint32_t recordNumber;
    };
    std::vector<HeldMatch> heldMatches;
    std::string heldEvent;
    bool releaseHeld = false;
    SimpleTimer heldRetryTimer;
    std::vector<size_t> eventRejected;
    Outcome outcome = Outcome::NONE;
    uint32_t generation = 0;
    RecordKind sentKind = RecordKind::MATCH;
    size_t sentCount = 0;
    uint32_t sentNumber = 0;
    int answeredStatus = 0;
    FdnRecordStore::MatchRing::Entry matchBatch[FDN_MATCH_RECORD_CAPACITY] = {};
};
