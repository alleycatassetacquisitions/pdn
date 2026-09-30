#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "device/device-type.hpp"
#include "device/drivers/firmware-store-interface.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "utils/simple-timer.hpp"

/// Collects a firmware image into the inactive flash slot as OFFER and CHUNK
/// frames arrive. Chunks land in whatever order the radio delivers them; a
/// bitmap tracks what has landed so a later repair round can ask for gaps.
/// Depends only on PeerCommsInterface and FirmwareStoreInterface, so it
/// builds and runs in the native test suite with no ESP32 headers in reach.
class FirmwareUpdateManager {
public:
    /// Wires the manager to the radio and flash slot and registers to
    /// receive kFirmwareUpdate frames. rootPublicKey pins the chain every
    /// offer must verify against. isEligible gates onOffer: an offer is
    /// never even evaluated unless it returns true (e.g. idle, no cable,
    /// no match in progress). deviceType is this device's own build
    /// (Device::getDeviceType()); onOffer refuses any offer built for a
    /// different one, and sendOffer stamps it onto every outgoing offer —
    /// beginSeeding's first broadcast and every rebroadcast after it.
    FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                          const uint8_t* rootPublicKey, std::function<bool()> isEligible, DeviceType deviceType);

    /// Deregisters both radio handlers. The driver's tables hold a raw
    /// pointer to this manager, and ~Quickdraw deletes it while the radio is
    /// still live, so leaving them registered would dispatch a later
    /// kFirmwareUpdate frame into freed memory.
    ~FirmwareUpdateManager();

    /// Evaluates an announced image. Ignores it outright unless the receive
    /// side is idle — a transfer in progress, a queued completion report and
    /// a pending post-commit restart are all one phase check — or if it
    /// matches the running image; otherwise, once it verifies and fits the
    /// inactive slot, opens the slot for the declared length and starts
    /// collecting chunks.
    void onOffer(const uint8_t* fromMac, const FirmwareOfferPayload& offer);

    /// Writes one chunk into the open slot at its declared index. Drops it
    /// if no offer is open, or if its index or declared length is out of
    /// range for that offer; an index that already landed is written again,
    /// so honest bytes can replace a forgery, but counts no further
    /// progress. Once every index has landed, hashes the assembled image,
    /// re-verifies the offer it was accepted under, writes the trailer, and
    /// commits — or reports why it did not.
    void onChunk(const FirmwareChunkHeader& header, const uint8_t* data);

    /// A seed asking who's still missing chunks for `poll`'s image. Ignored
    /// unless it names the image currently being collected. Otherwise arms a
    /// random-delay reply so every receiver answering the same poll doesn't
    /// broadcast STATUS in the same instant.
    void onPoll(const FirmwarePollPayload& poll);

    /// A receiver's report of what it has for the run currently being
    /// seeded. Ignored unless it names this run's image; otherwise the
    /// chunks it did not report folds into the set the next repair sweep
    /// resends. Carries no sender MAC: the repair set is a union over every
    /// reply heard, not a per-device ledger.
    void onStatus(const FirmwareStatusPayload& status);

    /// True once an offer has been accepted. Cleared as soon as collection
    /// ends, whether the commit that follows passes or fails, when the
    /// device becomes ineligible mid-transfer and sync() aborts it, and when
    /// no chunk has landed for long enough that the seed is gone.
    bool isReceiving() const;

    /// Count of distinct chunk indices written so far.
    uint16_t receivedCount() const;

    /// Copies the receive bitmap (bit N set means chunk N landed) into `out`.
    void fillBitmap(uint8_t out[FIRMWARE_BITMAP_BYTES]) const;

    /// Starts distributing the device's own running image: uses the
    /// already-cached hash, reads the signer certificate and image
    /// signature from the trailer past the declared image length, and
    /// broadcasts the first OFFER. False if a run is already streaming or
    /// the trailer cannot be read.
    bool beginSeeding();

    /// Reports what the radio did with the last frame this manager sent for
    /// kFirmwareUpdate. Clears the in-flight gate either way, and no frame is
    /// resent on the strength of a failure: a lost CHUNK is the repair loop's
    /// job, a lost STATUS is re-answered at the seed's next POLL, and a lost
    /// COMPLETE is not recovered at all — nothing in the fleet decodes it,
    /// and the restart sequenced behind it must not wait on it.
    void onSendReport(bool success);

    /// True once beginSeeding has armed streaming.
    bool isSeeding() const;

    /// Sends the next frame once the radio has cleared the last one: an
    /// OFFER when the one-second cadence has elapsed, otherwise the next
    /// unstreamed chunk. The only place a frame is sent, and the only place
    /// the post-commit restart fires from. Pumped from src/pdn/main.cpp's
    /// loop(): receiving an offer is passive, and Device::loop() dispatches
    /// onStateLoop to the active app alone, so anything pumped from a state
    /// machine stops being serviced the moment another app takes over.
    void sync();

    /// Routes a raw radio frame to onOffer/onChunk/onPoll/onStatus, validating
    /// its length against the relevant wire struct before any cast. Public so
    /// the driver's packet callback (dispatchPacket) can reach it and so a
    /// test can drive the wire-receive path directly without a driver.
    void onPacketReceived(const uint8_t* src, const uint8_t* data, size_t length);

private:
    // Stage of an inbound transfer. Linear, and the receive side is only ever
    // in one of these: an accepted offer moves IDLE -> RECEIVING, a full
    // bitmap moves RECEIVING -> REPORTING whether the commit passed or
    // failed, and REPORTING ends at RESTARTING (commit passed, report
    // shipped) or back at IDLE (commit failed). completeResult, not a phase,
    // says which.
    enum class ReceivePhase : uint8_t {
        IDLE = 0,
        RECEIVING = 1,
        REPORTING = 2,
        RESTARTING = 3,
    };

    // Stage of the run this device is distributing. Also linear:
    // beginSeeding moves IDLE -> STREAMING, the last streamed chunk moves
    // STREAMING -> POLLING, a POLL whose window found gaps moves POLLING ->
    // REPAIRING, and a resend round ends back at POLLING. A round that finds
    // nothing missing, or one whose gap set stops shrinking, ends at IDLE.
    enum class SeedPhase : uint8_t {
        IDLE = 0,
        STREAMING = 1,
        POLLING = 2,
        REPAIRING = 3,
    };

    // Trampoline for the driver's packet callback; forwards straight to
    // onPacketReceived.
    static void dispatchPacket(const uint8_t* src, const uint8_t* data, size_t length, void* ctx);

    // Trampoline the driver calls on terminal success or final give-up for a
    // kFirmwareUpdate frame; forwards straight to onSendReport.
    static void dispatchSendStatus(const uint8_t* dstMac, const uint8_t* data, size_t length, bool success,
                                   void* ctx);

    void sendOffer();
    void sendNextChunk();
    void sendStatus();

    // Hands one frame to the radio and latches the single-frame gate. False
    // means nothing was queued, so no send report will ever arrive for it
    // and the caller must leave its own progress alone for sync() to retry.
    // `what` names the caller in the log line.
    bool sendFrame(const uint8_t* data, size_t length, const char* what);
    // Reads chunk `index` out of the running image and sends it. Shared by
    // the initial stream and the repair rounds, which differ only in which
    // cursor they advance once it is queued.
    bool sendChunkAt(uint16_t index, const char* what);

    // Discards an in-progress receive and returns the phase to IDLE, so the
    // next offer heard can open the slot again. The caller logs why.
    void abortReceive();

    // Runs once the receive bitmap is full: hashes the assembled image,
    // checks it against the offer, re-verifies the offer's signature chain,
    // writes the trailer, and only then marks the slot bootable. Any failure
    // aborts the write and queues why for sendComplete.
    void evaluateCommit();
    // Sends completeResult behind the same single-frame-in-flight gate as
    // every other outbound frame; reached from sync() while the receive
    // phase is REPORTING rather than called by evaluateCommit directly.
    void sendComplete(FirmwareResult result);

    // Drives the seed's state once its initial broadcast has streamed every
    // chunk: send POLL, wait out the collection window, resend the union of
    // reported gaps, repeat until a round comes back clean or stalls.
    void syncRepair();
    void sendPoll();
    void sendNextRepairChunk();
    void resolveRepairRound();

    bool bitmapBit(uint16_t index) const;
    void setBitmapBit(uint16_t index);

    // The running image's bytes cannot change while it is executing, so its
    // hash is computed at most once per boot and reused from then on.
    const uint8_t* cachedRunningImageHash();

    // Next draw from this device's own PRNG state, seeded from its MAC at
    // construction — never the global rand()/srand(), whose state this
    // firmware shares with unrelated code (symbol-manager's fixed
    // std::srand(35), player.cpp's user-ID seed).
    uint32_t nextRandomUint32();

    PeerCommsInterface* peerComms;
    FirmwareStoreInterface* firmwareStore;
    const uint8_t* rootPublicKey;
    // Gates onOffer: consulted before anything else, including the
    // phase/deviceType/geometry guards below.
    std::function<bool()> isEligible;
    // This device's own build; onOffer refuses any offer whose deviceType
    // field does not match, and sendOffer stamps outgoing offers with it.
    DeviceType deviceType;
    uint32_t rngState;

    ReceivePhase receivePhase = ReceivePhase::IDLE;
    uint16_t chunkCount = 0;
    uint16_t chunkSize = 0;
    uint16_t receivedChunkCount = 0;
    size_t imageLength = 0;
    uint8_t currentImageHash[FIRMWARE_SHA256_LENGTH] = {};
    uint8_t bitmap[FIRMWARE_BITMAP_BYTES] = {};

    // Latched the instant any chunk's writeAt fails; checked ahead of the
    // hash comparison at commit time so a hardware write fault is reported
    // as FLASH_FAILED rather than the confusing BAD_HASH its unwritten
    // region would otherwise produce.
    bool writeFailed = false;
    // The exact offer this transfer was accepted under: re-verified at
    // commit time, and its cert/signature seed the trailer written past the
    // image once that passes.
    FirmwareOfferPayload acceptedOffer = {};

    // What REPORTING reports: set by evaluateCommit, read by sendComplete
    // when sync() flushes the report, and the difference between a REPORTING
    // that ends at RESTARTING and one that ends back at IDLE.
    FirmwareResult completeResult = FirmwareResult::OK;
    // Refused attempts to hand COMPLETE to the radio, counted so REPORTING
    // cannot hold the post-commit restart hostage to a frame the radio will
    // not take. Reset by evaluateCommit for each transfer.
    int completeAttempts = 0;

    bool runningImageHashComputed = false;
    uint8_t runningImageHash[FIRMWARE_SHA256_LENGTH] = {};

    // A poll answer is deferred behind a random delay so every receiver
    // hearing the same POLL doesn't reply in the same instant; armed by
    // onPoll, fired by sync() once it expires.
    SimpleTimer statusReplyTimer;

    // Rate limiter on offer signature verification, which is the first
    // expensive thing an unauthenticated OFFER can make this device do.
    // Armed whenever a verification runs, whatever its result.
    SimpleTimer offerVerifyTimer;

    // Deadline on a transfer making progress, not on its total length: armed
    // when an offer is accepted and re-armed by every chunk that lands, so a
    // slow-but-healthy image is never cut off while an abandoned one still
    // ends. Read only while the phase is RECEIVING, and every entry into
    // RECEIVING re-arms it, so it needs no invalidation on the way out.
    SimpleTimer receiveDeadlineTimer;

    // Kept separate from the receive fields above: a device offering its own
    // image and collecting someone else's at the same time must not have one
    // role's beginSeeding corrupt the other's in-progress geometry.
    SeedPhase seedPhase = SeedPhase::IDLE;
    bool sendInFlight = false;
    uint16_t seedChunkCount = 0;
    uint16_t seedNextChunkIndex = 0;
    size_t seedImageLength = 0;
    // Fixed for the run's lifetime at beginSeeding(): onStatus/sendPoll/
    // sendOffer all send or compare against this, not a freshly recomputed
    // hash, so the run's identity can't drift mid-run even though it
    // happens to equal cachedRunningImageHash() today.
    uint8_t seedImageHash[FIRMWARE_SHA256_LENGTH] = {};
    SimpleTimer offerTimer;
    SignerCert seedCert = {};
    uint8_t seedImageSignature[FIRMWARE_SIG_LENGTH] = {};

    // Repair loop: repairBitmap accumulates the complement of every STATUS
    // heard since the last round resolved; resolveRepairRound freezes it
    // into repairStreamSet (what this round actually resends) and clears
    // repairBitmap so the next round starts collecting immediately, not only
    // once its own POLL goes out. lastRepairSet/identicalRepairRounds detect
    // a repair set that stops shrinking so a permanently-missing chunk (one
    // that never lands) doesn't broadcast forever.
    uint8_t repairBitmap[FIRMWARE_BITMAP_BYTES] = {};
    uint8_t repairStreamSet[FIRMWARE_BITMAP_BYTES] = {};
    uint8_t lastRepairSet[FIRMWARE_BITMAP_BYTES] = {};
    bool haveLastRepairSet = false;
    int identicalRepairRounds = 0;
    uint16_t repairCursor = 0;
    // Running means this round's POLL is out and its replies are still being
    // collected; POLLING with it stopped means the POLL has yet to be
    // queued. Invalidated on every entry into POLLING so a previous round's
    // expired window cannot resolve the next one before its POLL goes out.
    SimpleTimer pollWindowTimer;
};
