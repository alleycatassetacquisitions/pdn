#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "alleycat-server/crash-api.hpp"
#include "alleycat-server/fdn-record-store.hpp"
#include "alleycat-server/fdn-stored-entry.hpp"
#include "alleycat-server/matches-api.hpp"
#include "alleycat-server/quickdraw-match-codec.hpp"
#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "utils/uuid-bytes.hpp"
#include "wireless/record-relay-types.hpp"
#include "wireless/record-source.hpp"
#include "wireless/wireless-types.hpp"

/**
 * How long one upload cycle may keep the radio on Wi-Fi. With no AP in range a
 * queued request never fails on its own.
 */
inline constexpr unsigned long FDN_UPLOAD_TIMEOUT_MS = 15000;

static_assert(FDN_MATCH_RECORD_CAPACITY <= QuickdrawMatchCodec::MAX_MATCHES_PER_BATCH,
              "one upload must be able to drain every stored match record");

/** Crash records waiting to be uploaded, oldest first. */
class FdnCrashUploadQueue {
public:
    /** A queue is owned by whoever built it. */
    virtual ~FdnCrashUploadQueue() = default;

    /**
     * Fills @p entry with the next record to upload and the MAC address of the
     * device it is from. Reading does not retire the record it hands out.
     * @return false when the queue has nothing to upload.
     */
    virtual bool readNext(FdnStoredCrashEntry& entry) = 0;

    /**
     * Retires the record readNext() handed out last.
     * @return false when it could not be retired, so readNext() would hand
     * the same record out again.
     */
    virtual bool retireLastRead() = 0;

    /**
     * @return true when a record whose body the server refused is retired, as
     * one the server will never take; false when it stays for the next cycle.
     */
    virtual bool isRetiredOnBodyRefusal() const = 0;

    /** @return The word the log calls this queue's records by. */
    virtual const char* getLabel() const = 0;
};

/** The FDN's own crash records: those above the sent mark of its crash record source. */
class FdnOwnCrashQueue : public FdnCrashUploadQueue {
public:
    /** @param ownCrashSource Not owned; its mark is the FDN's sent mark. */
    FdnOwnCrashQueue(WirelessManager* wirelessManager,
                     RecordSource<CrashRecordTraits>* ownCrashSource)
        : wirelessManager(wirelessManager)
        , ownCrashSource(ownCrashSource) {}

    /** @return true when the source holds a readable record above its mark. */
    bool hasPending() {
        uint32_t recordNumber = 0;
        CrashRecordContent content{};
        return ownCrashSource->readLowestPending(recordNumber, content);
    }

    /** Reads the lowest pending record of the source, under the FDN's own MAC address. */
    bool readNext(FdnStoredCrashEntry& entry) override {
        uint32_t recordNumber = 0;
        if (!ownCrashSource->readLowestPending(recordNumber, entry.content)) {
            return false;
        }
        memcpy(entry.senderMac, wirelessManager->getMacAddress(), sizeof(entry.senderMac));
        entry.kind = static_cast<uint8_t>(RecordKind::CRASH);
        entry.recordNumber = recordNumber;
        lastReadNumber = recordNumber;
        return true;
    }

    /**
     * Moves the source's mark to the record read last. The mark is kept in
     * storage, which can refuse the write; the record is then still pending.
     */
    bool retireLastRead() override {
        ownCrashSource->advanceMark(lastReadNumber);
        uint32_t pendingNumber = 0;
        CrashRecordContent content{};
        return !ownCrashSource->readLowestPending(pendingNumber, content) ||
               pendingNumber > lastReadNumber;
    }

    /** Nothing retires one of the FDN's own records but the server taking it. */
    bool isRetiredOnBodyRefusal() const override { return false; }

    /** @return "own". */
    const char* getLabel() const override { return "own"; }

private:
    WirelessManager* wirelessManager = nullptr;
    RecordSource<CrashRecordTraits>* ownCrashSource = nullptr;
    uint32_t lastReadNumber = 0;
};

/** The crash records PDNs relayed into the record store. */
class FdnRelayedCrashQueue : public FdnCrashUploadQueue {
public:
    /** @param fdnRecordStore Not owned. */
    explicit FdnRelayedCrashQueue(FdnRecordStore* fdnRecordStore)
        : fdnRecordStore(fdnRecordStore) {}

    /** Reads the oldest relayed crash record the store holds, under its sender's MAC address. */
    bool readNext(FdnStoredCrashEntry& entry) override {
        return fdnRecordStore->readOldestCrash(entry, lastReadPosition);
    }

    /** Retires the record read last; the store refuses if it is no longer its oldest. */
    bool retireLastRead() override { return fdnRecordStore->retireCrash(lastReadPosition); }

    /**
     * One relayed record the server will never take would otherwise block
     * every record behind it for good.
     */
    bool isRetiredOnBodyRefusal() const override { return true; }

    /** @return "relayed". */
    const char* getLabel() const override { return "relayed"; }

private:
    FdnRecordStore* fdnRecordStore = nullptr;
    uint32_t lastReadPosition = 0;
};

/**
 * Uploads the FDN's own crash records and the records PDNs relayed, one cycle
 * at a time; sync() hands the radio back to peer-to-peer when the cycle ends or
 * after FDN_UPLOAD_TIMEOUT_MS. A cycle owns Wi-Fi from its start to its end,
 * and does not start while another component owns it.
 *
 * Any decodable match response retires the batch; the server never overwrites
 * a stored match, so a retry is safe.
 */
class FdnRecordUploader {
public:
    /**
     * @param ownCrashSource The FDN's own crash records; its mark is the FDN's
     * sent mark. Neither it nor @p fdnRecordStore is owned by the uploader.
     */
    FdnRecordUploader(WirelessManager* wirelessManager,
                      RecordSource<CrashRecordTraits>* ownCrashSource,
                      FdnRecordStore* fdnRecordStore)
        : wirelessManager(wirelessManager)
        , fdnRecordStore(fdnRecordStore)
        , ownCrashQueue(wirelessManager, ownCrashSource)
        , relayedCrashQueue(fdnRecordStore) {}

    /**
     * @return true when a record of any kind waits to be uploaded: a relayed
     * match record, a relayed crash record, or a crash record of the FDN's own.
     */
    bool hasRecordsWaiting() {
        return fdnRecordStore->getStoredCount(RecordKind::MATCH) > 0 ||
               fdnRecordStore->getStoredCount(RecordKind::CRASH) > 0 || ownCrashQueue.hasPending();
    }

    /**
     * Starts a cycle that uploads everything waiting. Boot and the periodic
     * uplink both call this. A record the cycle fails to upload stays where it
     * is for the next one.
     * @return false when no cycle started: one is already running, another
     * component owns Wi-Fi, or nothing could be sent. The radio is then left alone.
     */
    bool uploadAllRecords() {
        if (uploadTimer.isRunning()) {
            LOG_W("FDN_UPLOAD", "An upload cycle is already running");
            return false;
        }
        if (!wirelessManager->acquireWifi(this)) {
            LOG_I("FDN_UPLOAD", "Wi-Fi is in use; the records wait for the next uplink");
            return false;
        }
        cycleFailed = false;
        uploadAttempted = false;
        uploadTimer.setTimer(FDN_UPLOAD_TIMEOUT_MS);
        runStepsFrom(Step::MATCH_BATCH);
        if (!uploadAttempted) {
            // Nothing was queued, so the radio never left peer-to-peer.
            endCycle();
            return false;
        }
        return true;
    }

    /**
     * Ends the running cycle once every request of it is answered, or once
     * FDN_UPLOAD_TIMEOUT_MS has passed, and gives Wi-Fi up. Call every tick.
     */
    void sync() {
        if (!uploadTimer.isRunning()) {
            return;
        }
        const bool requestsQueued = wirelessManager->hasQueuedHttpRequests();
        if (requestsQueued && !uploadTimer.expired()) {
            return;
        }
        if (requestsQueued) {
            LOG_W("FDN_UPLOAD", "Uploads timed out with a request unanswered");
        }
        endCycle();
    }

    /** @return true from the start of a cycle until sync() ends it. */
    bool isUploading() { return uploadTimer.isRunning(); }

    /**
     * @return true when an upload in the most recent cycle failed, one failed
     * by the timeout included; a retired relayed crash record is not a failure.
     */
    bool didLastCycleFail() const { return cycleFailed; }

private:
    // The order of a cycle, stated here and nowhere else. The match batch is
    // first: matches decide prizes, and a long crash queue must not use up the
    // cycle's time ahead of them.
    enum class Step : uint8_t { MATCH_BATCH, OWN_CRASH_RECORDS, RELAYED_CRASH_RECORDS, FINISHED };

    static Step stepAfter(Step step) { return static_cast<Step>(static_cast<uint8_t>(step) + 1); }

    // Statuses that refuse the body itself, which the server would refuse
    // again; any other failure may pass on a later cycle.
    static bool isBodyRefusal(int httpStatus) {
        return httpStatus == 400 || httpStatus == 413 || httpStatus == 415 || httpStatus == 422;
    }

    // Wi-Fi goes first: at the deadline the requests this fails must still find
    // the timer expired, or their failures would start the next step.
    void endCycle() {
        wirelessManager->releaseWifi(this);
        uploadTimer.invalidate();
    }

    // Runs @p step, and the steps after it for as long as a step has nothing
    // to send. A step that put a request out comes back here from its answer.
    void runStepsFrom(Step step) {
        // Past the deadline the radio is on its way back to peer-to-peer, and
        // a request queued now would only be failed with the rest.
        if (uploadTimer.expired()) {
            return;
        }
        while (step != Step::FINISHED && !startStep(step)) {
            step = stepAfter(step);
        }
    }

    // @return true when the step put a request out, or tried to.
    bool startStep(Step step) {
        // The store's reads change nothing, so an unreadable record at the
        // head, which would block every record behind it, is dropped here.
        switch (step) {
            case Step::MATCH_BATCH:
                fdnRecordStore->dropUnreadableOldest(RecordKind::MATCH);
                return uploadMatchBatch();
            case Step::OWN_CRASH_RECORDS: return uploadNextCrash(step, &ownCrashQueue);
            case Step::RELAYED_CRASH_RECORDS:
                fdnRecordStore->dropUnreadableOldest(RecordKind::CRASH);
                return uploadNextCrash(step, &relayedCrashQueue);
            case Step::FINISHED: return false;
        }
        return false;
    }

    bool uploadMatchBatch() {
        uint32_t batchPosition = 0;
        const size_t batchCount = fdnRecordStore->countReadableMatchBatch(batchPosition);
        if (batchCount == 0) {
            return false;
        }
        uploadAttempted = true;
        MatchesApi::uploadQuickdrawMatches(
        wirelessManager, batchCount,
        [this, batchPosition](size_t index, FdnStoredMatchEntry& entry) {
            return fdnRecordStore->readMatch(batchPosition + static_cast<uint32_t>(index), entry);
        },
        [](const QuickdrawMatchRejection& rejection) {
            char uuidText[UUIDBytes::TEXT_BUFFER_SIZE] = {};
            rejection.matchId.format(uuidText);
            LOG_W("FDN_UPLOAD", "Server rejected match[%u] %s (code=%d)",
                  static_cast<unsigned>(rejection.matchIndex), uuidText,
                  static_cast<int>(rejection.rawCode));
        },
        [this, batchPosition, batchCount](const QuickdrawUploadResult&) {
            fdnRecordStore->retireMatchBatch(batchPosition, batchCount);
            runStepsFrom(stepAfter(Step::MATCH_BATCH));
        },
        [this](const WirelessErrorInfo& error) {
            LOG_W("FDN_UPLOAD", "Match upload failed: %s; the batch stays stored",
                  error.message.c_str());
            cycleFailed = true;
            runStepsFrom(stepAfter(Step::MATCH_BATCH));
        });
        return true;
    }

    // Uploads the next record of @p queue. Its answer runs @p step again for
    // the record after it, so one crash request is out at a time.
    bool uploadNextCrash(Step step, FdnCrashUploadQueue* queue) {
        FdnStoredCrashEntry entry{};
        if (!queue->readNext(entry)) {
            return false;
        }

        const uint32_t recordNumber = entry.recordNumber;
        uploadAttempted = true;
        CrashApi::uploadCrash(
        wirelessManager, entry,
        [this, step, queue, recordNumber](const std::string&) {
            LOG_I("FDN_UPLOAD", "Uploaded %s crash #%lu", queue->getLabel(),
                  static_cast<unsigned long>(recordNumber));
            retireAndContinue(step, queue);
        },
        [this, step, queue, recordNumber](const WirelessErrorInfo& error) {
            if (queue->isRetiredOnBodyRefusal() && isBodyRefusal(error.httpStatus)) {
                LOG_W("FDN_UPLOAD", "Server refused %s crash #%lu for good (HTTP %d)",
                      queue->getLabel(), static_cast<unsigned long>(recordNumber),
                      error.httpStatus);
                retireAndContinue(step, queue);
                return;
            }
            LOG_E("FDN_UPLOAD", "Failed to upload %s crash #%lu: %s; it stays pending",
                  queue->getLabel(), static_cast<unsigned long>(recordNumber),
                  error.message.c_str());
            cycleFailed = true;
            // The records behind a failed one wait with it; the next step does not.
            runStepsFrom(stepAfter(step));
        });
        return true;
    }

    // A record that cannot be retired would be read and sent again at once,
    // until the cycle timed out. Ending its step as failed bounds the repeat to
    // one upload of it per cycle; it goes up again on the next one.
    void retireAndContinue(Step step, FdnCrashUploadQueue* queue) {
        if (queue->retireLastRead()) {
            runStepsFrom(step);
            return;
        }
        LOG_E("FDN_UPLOAD", "An uploaded %s crash record could not be retired", queue->getLabel());
        cycleFailed = true;
        runStepsFrom(stepAfter(step));
    }

    WirelessManager* wirelessManager = nullptr;
    FdnRecordStore* fdnRecordStore = nullptr;
    FdnOwnCrashQueue ownCrashQueue;
    FdnRelayedCrashQueue relayedCrashQueue;
    // Running for exactly as long as a cycle is.
    SimpleTimer uploadTimer;
    bool uploadAttempted = false;
    bool cycleFailed = false;
};
