#pragma once

#include <cstddef>
#include <cstdint>

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
    /// offer must verify against.
    FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                          const uint8_t* rootPublicKey);

    /// Evaluates an announced image. Ignores it outright if it matches the
    /// running image or the image already being collected; otherwise, once
    /// it verifies and fits the inactive slot, opens the slot for the
    /// declared length and starts collecting chunks.
    void onOffer(const uint8_t* fromMac, const FirmwareOfferPayload& offer);

    /// Writes one chunk into the open slot at its declared index. Drops it
    /// if no offer is open, the index is out of range for it, or that index
    /// already landed. Once every index has landed, hashes the assembled
    /// image, re-verifies the offer it was accepted under, writes the
    /// trailer, and commits — or reports why it did not.
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

    /// True once an offer has been accepted; nothing here clears it back to
    /// false.
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
    /// kFirmwareUpdate. Clears the in-flight gate either way: a lost frame
    /// is the repair loop's job, not a per-frame retry here.
    void onSendReport(bool success);

    /// True once beginSeeding has armed streaming.
    bool isSeeding() const;

    /// Sends the next frame once the radio has cleared the last one: an
    /// OFFER when the one-second cadence has elapsed, otherwise the next
    /// unstreamed chunk. The only place a frame is sent; the owning state
    /// calls this every tick.
    void sync();

private:
    // Routes a raw radio frame to onOffer/onChunk, validating its length
    // against the wire struct before any cast — verifyOffer takes a const&
    // and cannot check the frame it was handed.
    static void dispatchPacket(const uint8_t* src, const uint8_t* data, size_t length, void* ctx);
    void onPacketReceived(const uint8_t* src, const uint8_t* data, size_t length);

    // Trampoline the driver calls on terminal success or final give-up for a
    // kFirmwareUpdate frame; forwards straight to onSendReport.
    static void dispatchSendStatus(const uint8_t* dstMac, const uint8_t* data, size_t length, bool success,
                                   void* ctx);

    void sendOffer();
    void sendNextChunk();
    void sendStatus();

    // Runs once the receive bitmap is full: hashes the assembled image,
    // checks it against the offer, re-verifies the offer's signature chain,
    // writes the trailer, and only then marks the slot bootable. Any failure
    // aborts the write and queues why for sendComplete.
    void evaluateCommit();
    // Sends completeResult behind the same single-frame-in-flight gate as
    // every other outbound frame; queued by evaluateCommit via
    // completePending rather than called directly.
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
    uint32_t rngState;

    bool receiving = false;
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

    // Set by evaluateCommit, sent from sync() like every other outbound
    // frame once whichever frame is already in flight clears.
    bool completePending = false;
    FirmwareResult completeResult = FirmwareResult::OK;

    bool runningImageHashComputed = false;
    uint8_t runningImageHash[FIRMWARE_SHA256_LENGTH] = {};

    // A poll answer is deferred behind a random delay so every receiver
    // hearing the same POLL doesn't reply in the same instant; armed by
    // onPoll, fired by sync() once it expires.
    SimpleTimer statusReplyTimer;

    // Kept separate from the receiving fields above: a device offering its
    // own image and collecting someone else's at the same time must not have
    // one role's beginSeeding corrupt the other's in-progress geometry.
    bool seeding = false;
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
    bool pollSentForRound = false;
    bool repairRoundReady = false;
    uint16_t repairCursor = 0;
    SimpleTimer pollWindowTimer;
};
