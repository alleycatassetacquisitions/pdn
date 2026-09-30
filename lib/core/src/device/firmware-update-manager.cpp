#include "device/firmware-update-manager.hpp"

#include "device/firmware-verify.hpp"
#include "device/drivers/logger.hpp"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

constexpr const char* TAG = "FirmwareUpdate";

// Bytes per chunk the seed streams at. Fixed rather than negotiated: the
// build-time signer signs imageSha256|imageLength|chunkSize|chunkCount|
// deviceType, so the device must reproduce the exact chunkSize/chunkCount
// the signature covers rather than choosing one at runtime.
constexpr uint16_t SEED_CHUNK_SIZE = 1400;

// OTA_PARTITION_SIZE (firmware-store-interface.hpp) is the partitions.csv
// slot size; tied here to FIRMWARE_MAX_CHUNKS so a partition resize that
// would overflow repairBitmap fails the build instead of silently
// corrupting it at runtime.
static_assert((OTA_PARTITION_SIZE + SEED_CHUNK_SIZE - 1) / SEED_CHUNK_SIZE <= FIRMWARE_MAX_CHUNKS,
              "an OTA partition no longer fits FIRMWARE_MAX_CHUNKS chunks at SEED_CHUNK_SIZE");

// How often the seed re-broadcasts OFFER for the life of a run.
constexpr unsigned long SEED_OFFER_INTERVAL_MS = 1000;

// How long the seed waits after a POLL for STATUS replies before acting on
// whatever it heard. Must clear onPoll's [0, STATUS_BACKOFF_CEILING_MS)
// reply jitter with margin, or a slow-but-honest reply arrives after the
// seed already moved on.
constexpr unsigned long SEED_POLL_WINDOW_MS = 600;

// Upper bound (exclusive) of a receiver's random delay before answering a
// POLL, so every device that heard it doesn't reply in the same instant.
constexpr unsigned long STATUS_BACKOFF_CEILING_MS = 500;

// A repair set that stops shrinking for this many rounds in a row means at
// least one chunk is never going to land; broadcasting it forever would
// never end the run, so the seed gives up on it instead.
constexpr int REPAIR_STALL_ROUNDS = 3;

// How long a receive may go without a chunk landing before its seed counts
// as gone. Measured from the last chunk accepted, never from the offer: a
// 2.68MB image is ~1900 chunks of streaming plus however many repair rounds
// the run needs, so a cap on the total would cut off a slow-but-healthy
// transfer. Ten seconds of silence is a seed that died, not a slow one.
constexpr unsigned long RECEIVE_STALL_TIMEOUT_MS = 10000;

// Times sendComplete may be refused by the radio before the report is
// dropped and the phase moves on. Deliberately small: nothing in the fleet
// decodes COMPLETE, so it is a diagnostic, while the restart sequenced behind
// it is what makes the rollback window mean anything.
constexpr int COMPLETE_SEND_ATTEMPTS = 5;

bool bitAt(const uint8_t bitmap[FIRMWARE_BITMAP_BYTES], uint16_t index) {
    return (bitmap[index / 8] & (1 << (index % 8))) != 0;
}

void setBitAt(uint8_t bitmap[FIRMWARE_BITMAP_BYTES], uint16_t index) {
    bitmap[index / 8] |= static_cast<uint8_t>(1 << (index % 8));
}

bool bitmapIsEmpty(const uint8_t bitmap[FIRMWARE_BITMAP_BYTES]) {
    for (size_t i = 0; i < FIRMWARE_BITMAP_BYTES; i++) {
        if (bitmap[i] != 0) {
            return false;
        }
    }
    return true;
}

// Folds all six MAC bytes into a nonzero xorshift32 seed, so two devices
// differing in any byte start from different state. xorshift32 never
// escapes an all-zero state, so a (pathological) all-zero MAC still needs a
// usable seed.
uint32_t seedFromMac(const uint8_t mac[6]) {
    uint32_t seed = 0x9E3779B9u;
    for (size_t i = 0; i < 6; i++) {
        seed = (seed * 33u) ^ mac[i];
    }
    return seed != 0 ? seed : 1u;
}

// Hashes `length` bytes read through `read` (readRunningImage for the booted
// image, readWrittenSlot for a just-assembled one — the two things this file
// ever needs a SHA-256 of), streaming a fixed buffer rather than holding the
// whole image: it can be several megabytes.
void hashRange(FirmwareStoreInterface* firmwareStore,
               bool (FirmwareStoreInterface::*read)(size_t, uint8_t*, size_t) const, size_t length,
               uint8_t out[FIRMWARE_SHA256_LENGTH]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);

    uint8_t buffer[256];
    size_t offset = 0;
    while (offset < length) {
        const size_t chunk = std::min(sizeof(buffer), length - offset);
        if (!(firmwareStore->*read)(offset, buffer, chunk)) {
            LOG_E(TAG, "hashRange: read failed at offset %zu of %zu; hash is incomplete", offset, length);
            break;
        }
        mbedtls_sha256_update(&ctx, buffer, chunk);
        offset += chunk;
    }
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

}  // namespace

FirmwareUpdateManager::FirmwareUpdateManager(PeerCommsInterface* peerComms,
                                             FirmwareStoreInterface* firmwareStore,
                                             const uint8_t* rootPublicKey, std::function<bool()> isEligible,
                                             DeviceType deviceType)
    : peerComms(peerComms)
    , firmwareStore(firmwareStore)
    , rootPublicKey(rootPublicKey)
    , isEligible(std::move(isEligible))
    , deviceType(deviceType)
    , rngState(seedFromMac(peerComms->getMacAddress())) {
    peerComms->setPacketHandler(PktType::kFirmwareUpdate, &FirmwareUpdateManager::dispatchPacket, this);
    peerComms->setSendStatusHandler(PktType::kFirmwareUpdate, &FirmwareUpdateManager::dispatchSendStatus, this);
}

void FirmwareUpdateManager::dispatchPacket(const uint8_t* src, const uint8_t* data, size_t length, void* ctx) {
    static_cast<FirmwareUpdateManager*>(ctx)->onPacketReceived(src, data, length);
}

void FirmwareUpdateManager::onPacketReceived(const uint8_t* src, const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return;
    }
    switch (static_cast<FirmwareCmd>(data[0])) {
        case FirmwareCmd::OFFER:
            if (length < sizeof(FirmwareOfferPayload)) {
                LOG_E(TAG, "OFFER frame too short (%zu < %zu)", length, sizeof(FirmwareOfferPayload));
                return;
            }
            onOffer(src, *reinterpret_cast<const FirmwareOfferPayload*>(data));
            return;
        case FirmwareCmd::CHUNK: {
            if (length < sizeof(FirmwareChunkHeader)) {
                LOG_D(TAG, "CHUNK frame too short (%zu < %zu)", length, sizeof(FirmwareChunkHeader));
                return;
            }
            const FirmwareChunkHeader* header = reinterpret_cast<const FirmwareChunkHeader*>(data);
            const uint8_t* body = data + sizeof(FirmwareChunkHeader);
            const size_t bodyLength = length - sizeof(FirmwareChunkHeader);
            if (static_cast<size_t>(header->length) > bodyLength) {
                LOG_D(TAG, "CHUNK declares %u bytes, frame carries %zu", header->length, bodyLength);
                return;
            }
            onChunk(*header, body);
            return;
        }
        case FirmwareCmd::POLL:
            if (length < sizeof(FirmwarePollPayload)) {
                LOG_D(TAG, "POLL frame too short (%zu < %zu)", length, sizeof(FirmwarePollPayload));
                return;
            }
            onPoll(*reinterpret_cast<const FirmwarePollPayload*>(data));
            return;
        case FirmwareCmd::STATUS:
            if (length < sizeof(FirmwareStatusPayload)) {
                LOG_D(TAG, "STATUS frame too short (%zu < %zu)", length, sizeof(FirmwareStatusPayload));
                return;
            }
            onStatus(*reinterpret_cast<const FirmwareStatusPayload*>(data));
            return;
        default:
            return;  // OFFER, CHUNK, POLL and STATUS are the only frames handled on the receive side
    }
}

const uint8_t* FirmwareUpdateManager::cachedRunningImageHash() {
    if (!runningImageHashComputed) {
        hashRange(firmwareStore, &FirmwareStoreInterface::readRunningImage, firmwareStore->getRunningImageLength(),
                  runningImageHash);
        runningImageHashComputed = true;
    }
    return runningImageHash;
}

uint32_t FirmwareUpdateManager::nextRandomUint32() {
    // xorshift32.
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

void FirmwareUpdateManager::onOffer(const uint8_t* fromMac, const FirmwareOfferPayload& offer) {
    (void)fromMac;  // nothing unicasts a reply to the seed yet

    if (!isEligible()) {
        return;  // not idle, cabled, or mid-match: don't even look at the offer
    }

    if (static_cast<DeviceType>(offer.deviceType) != deviceType) {
        LOG_E(TAG, "offer targets device type %d, this device is %d", offer.deviceType,
              static_cast<int>(deviceType));
        return;  // a correctly-signed offer for another device type must still be refused
    }

    // One check for every stage that must not be interrupted, which is every
    // stage but IDLE. RECEIVING: first offer wins for the whole transfer,
    // whatever image a later one carries — two seeds on different builds both
    // repeat OFFER once a second, and reopening the slot for each would mean a
    // multi-second blocking erase every second with neither transfer ever
    // finishing — and a receive whose seed goes silent ends on
    // receiveDeadlineTimer, so losing a seed is not a lockout for the boot.
    // REPORTING: sendComplete reads currentImageHash when sync()
    // flushes the report, so accepting here would make that report name the
    // new image while carrying the old transfer's result code. RESTARTING:
    // the slot is verified and already the boot target; re-erasing it would
    // strand the device on the running image.
    if (receivePhase != ReceivePhase::IDLE) {
        return;
    }

    if (std::memcmp(offer.imageSha256, cachedRunningImageHash(), FIRMWARE_SHA256_LENGTH) == 0) {
        return;  // already running this image
    }

    if (static_cast<size_t>(offer.chunkCount) > FIRMWARE_MAX_CHUNKS) {
        LOG_E(TAG, "offer declares %u chunks, over the bitmap ceiling", offer.chunkCount);
        return;
    }
    if (static_cast<size_t>(offer.imageLength) > firmwareStore->getInactiveSlotSize()) {
        LOG_E(TAG, "offer declares %u bytes, over the inactive slot", offer.imageLength);
        return;
    }
    // A chunk's write offset (index * chunkSize) is trusted nowhere else, so
    // the declared geometry must tile the declared length exactly: chunkCount
    // chunks of chunkSize bytes covering imageLength, no more and no less.
    // Also rejects chunkSize == 0 (every index would write at offset 0) and
    // chunkCount == 0 (nothing could ever be marked received).
    if (offer.chunkSize == 0 || offer.chunkCount == 0) {
        LOG_E(TAG, "offer declares a zero chunk size or chunk count");
        return;
    }
    const uint64_t expectedChunkCount =
        (static_cast<uint64_t>(offer.imageLength) + offer.chunkSize - 1) / offer.chunkSize;
    if (expectedChunkCount != offer.chunkCount) {
        LOG_E(TAG, "offer's chunkCount %u does not tile imageLength %u at chunkSize %u",
              offer.chunkCount, offer.imageLength, offer.chunkSize);
        return;
    }

    // Cheapest checks first: two P-256 verifies cost ~100ms+ on the S3, and
    // ESP-NOW handlers run on the main loop, so only an offer surviving the
    // checks above reaches the curve operations.
    const FirmwareResult result = verifyOffer(offer, rootPublicKey, firmwareStore->getMinGeneration());
    if (result != FirmwareResult::OK) {
        LOG_E(TAG, "offer failed verification (%d)", static_cast<int>(result));
        return;
    }

    if (!firmwareStore->beginWrite(offer.imageLength)) {
        LOG_E(TAG, "beginWrite failed for a %u byte image", offer.imageLength);
        return;  // fail closed: the phase stays IDLE, so no chunk can land in a half-erased slot
    }

    std::memcpy(currentImageHash, offer.imageSha256, FIRMWARE_SHA256_LENGTH);
    imageLength = offer.imageLength;
    chunkCount = offer.chunkCount;
    chunkSize = offer.chunkSize;
    receivedChunkCount = 0;
    std::memset(bitmap, 0, sizeof(bitmap));
    writeFailed = false;
    acceptedOffer = offer;  // re-verified at commit time; its cert/signature seed the trailer written past the image
    receivePhase = ReceivePhase::RECEIVING;
    receiveDeadlineTimer.setTimer(RECEIVE_STALL_TIMEOUT_MS);
}

void FirmwareUpdateManager::abortReceive() {
    firmwareStore->abortWrite();
    receivePhase = ReceivePhase::IDLE;
    statusReplyTimer.invalidate();  // a bitmap for a dead transfer is worse than no reply
}

void FirmwareUpdateManager::onChunk(const FirmwareChunkHeader& header, const uint8_t* data) {
    if (receivePhase != ReceivePhase::RECEIVING || header.index >= chunkCount || bitmapBit(header.index)) {
        return;
    }

    const size_t offset = static_cast<size_t>(header.index) * chunkSize;
    if (offset > imageLength || header.length > imageLength - offset) {
        LOG_D(TAG, "chunk %u would write past the declared image length", header.index);
        return;
    }

    // A write failure still counts the chunk as landed: redelivering a
    // doomed offset would not help, and letting collection finish lets the
    // commit check report FLASH_FAILED precisely instead of never
    // completing at all.
    if (!firmwareStore->writeAt(offset, data, header.length)) {
        LOG_E(TAG, "writeAt failed for chunk %u; the slot write has failed", header.index);
        writeFailed = true;
    }

    setBitmapBit(header.index);
    receivedChunkCount++;
    receiveDeadlineTimer.setTimer(RECEIVE_STALL_TIMEOUT_MS);  // progress, so the deadline moves with it
    if (receivedChunkCount == chunkCount) {
        evaluateCommit();
    }
}

void FirmwareUpdateManager::evaluateCommit() {
    receivePhase = ReceivePhase::REPORTING;  // this transfer is concluding, pass or fail
    completeAttempts = 0;
    // Any poll answer still counting down belongs to a transfer that is over.
    // Letting it fire would hand the seed a partial bitmap for an image this
    // device has either committed or abandoned, and those phantom gaps fold
    // straight into the next repair round.
    statusReplyTimer.invalidate();

    const auto fail = [this](FirmwareResult result) {
        firmwareStore->abortWrite();
        completeResult = result;
    };

    if (writeFailed) {
        LOG_E(TAG, "commit: a chunk write failed during transfer; the slot is unusable");
        fail(FirmwareResult::FLASH_FAILED);
        return;
    }

    uint8_t assembledHash[FIRMWARE_SHA256_LENGTH];
    hashRange(firmwareStore, &FirmwareStoreInterface::readWrittenSlot, imageLength, assembledHash);
    if (std::memcmp(assembledHash, currentImageHash, FIRMWARE_SHA256_LENGTH) != 0) {
        LOG_E(TAG, "commit: assembled image hash does not match the offer");
        fail(FirmwareResult::BAD_HASH);
        return;
    }

    // Verification before commit: re-run the same chain the offer was
    // accepted under (spec's "Verification before commit"), not just trust
    // the accept-time result.
    const FirmwareResult verifyResult = verifyOffer(acceptedOffer, rootPublicKey, firmwareStore->getMinGeneration());
    if (verifyResult != FirmwareResult::OK) {
        LOG_E(TAG, "commit: offer failed re-verification (%d)", static_cast<int>(verifyResult));
        fail(verifyResult);
        return;
    }

    // The transfer never carries the trailer (cert/signature ride in the
    // OFFER instead), so it has to be written here or this device could
    // never seed the image onward: beginSeeding() reads it from exactly this
    // offset. Written while the store's write is still open, not after
    // finishWrite() closes it.
    FirmwareTrailer trailer{};
    trailer.cert = acceptedOffer.cert;
    std::memcpy(trailer.imageSignature, acceptedOffer.imageSignature, FIRMWARE_SIG_LENGTH);
    trailer.imageLength = static_cast<uint32_t>(imageLength);
    trailer.magic = FIRMWARE_TRAILER_MAGIC;
    if (!firmwareStore->writeAt(imageLength, reinterpret_cast<const uint8_t*>(&trailer), sizeof(trailer))) {
        LOG_E(TAG, "commit: failed to write the trailer past the image");
        fail(FirmwareResult::FLASH_FAILED);
        return;
    }

    if (!firmwareStore->finishWrite()) {
        LOG_E(TAG, "commit: finishWrite failed");
        fail(FirmwareResult::FLASH_FAILED);
        return;
    }

    if (!firmwareStore->setBootToWritten()) {
        LOG_E(TAG, "commit: setBootToWritten failed");
        // finishWrite() already closed the store's write; there is nothing left to abort.
        completeResult = FirmwareResult::FLASH_FAILED;
        return;
    }

    // Revocation is by generation: taking an image signed under a higher one
    // is what retires every certificate below it, including a leaked signer's.
    // Only here, past verification and commit — raising the floor on an offer
    // alone would let an unverified frame lock the device out of every
    // legitimate update that follows.
    if (acceptedOffer.cert.generation > firmwareStore->getMinGeneration()) {
        firmwareStore->setMinGeneration(acceptedOffer.cert.generation);
    }

    // REPORTING ends at RESTARTING for an OK result: the spec requires a
    // restart once the boot partition is set, or the confirm timer armed at
    // the next boot never arms anything. The running hash cannot change
    // without one either, so until it fires the phase itself is what stops a
    // repeat OFFER reopening this slot.
    completeResult = FirmwareResult::OK;
}

void FirmwareUpdateManager::onPoll(const FirmwarePollPayload& poll) {
    if (receivePhase != ReceivePhase::RECEIVING) {
        return;  // nothing to report on
    }
    if (std::memcmp(poll.imageSha256, currentImageHash, FIRMWARE_SHA256_LENGTH) != 0) {
        return;  // a poll for an image other than the one being collected
    }
    // Re-rolled on every poll heard, including a repeat: nothing here tracks
    // whether a reply is already pending, and a fresh roll is no worse than
    // whatever delay was already running.
    statusReplyTimer.setTimer(nextRandomUint32() % STATUS_BACKOFF_CEILING_MS);
}

void FirmwareUpdateManager::onStatus(const FirmwareStatusPayload& status) {
    if (seedPhase == SeedPhase::IDLE) {
        return;  // no run in progress for a report to apply to
    }
    if (std::memcmp(status.imageSha256, seedImageHash, FIRMWARE_SHA256_LENGTH) != 0) {
        return;  // a stale reply from another run must not pollute this one's repair set
    }
    // Bounded to seedChunkCount, not the full 3072-bit wire bitmap: a
    // reported gap past the image's real chunk count would ask the seed to
    // resend a chunk that does not exist.
    for (uint16_t i = 0; i < seedChunkCount; i++) {
        if (!bitAt(status.bitmap, i)) {
            setBitAt(repairBitmap, i);
        }
    }
}

bool FirmwareUpdateManager::isReceiving() const {
    return receivePhase == ReceivePhase::RECEIVING;
}

uint16_t FirmwareUpdateManager::receivedCount() const {
    return receivedChunkCount;
}

void FirmwareUpdateManager::fillBitmap(uint8_t out[FIRMWARE_BITMAP_BYTES]) const {
    std::memcpy(out, bitmap, FIRMWARE_BITMAP_BYTES);
}

bool FirmwareUpdateManager::bitmapBit(uint16_t index) const {
    return (bitmap[index / 8] & (1 << (index % 8))) != 0;
}

void FirmwareUpdateManager::setBitmapBit(uint16_t index) {
    bitmap[index / 8] |= static_cast<uint8_t>(1 << (index % 8));
}

void FirmwareUpdateManager::sendStatus() {
    FirmwareStatusPayload status{};
    status.command = static_cast<uint8_t>(FirmwareCmd::STATUS);
    std::memcpy(status.imageSha256, currentImageHash, FIRMWARE_SHA256_LENGTH);
    status.receivedCount = receivedChunkCount;
    std::memcpy(status.bitmap, bitmap, FIRMWARE_BITMAP_BYTES);

    if (!sendFrame(reinterpret_cast<const uint8_t*>(&status), sizeof(status), "sendStatus")) {
        return;  // statusReplyTimer stays expired: sync() retries this same reply next tick
    }
    statusReplyTimer.invalidate();
}

void FirmwareUpdateManager::sendComplete(FirmwareResult result) {
    FirmwareCompletePayload complete{};
    complete.command = static_cast<uint8_t>(FirmwareCmd::COMPLETE);
    std::memcpy(complete.imageSha256, currentImageHash, FIRMWARE_SHA256_LENGTH);
    complete.result = static_cast<uint8_t>(result);

    if (!sendFrame(reinterpret_cast<const uint8_t*>(&complete), sizeof(complete), "sendComplete")) {
        completeAttempts++;
        if (completeAttempts < COMPLETE_SEND_ATTEMPTS) {
            return;  // the phase stays REPORTING: sync() retries next tick
        }
        // A radio that will not take the frame must not cost the restart: it
        // is what points the device at the image it just verified.
        LOG_E(TAG, "sendComplete: refused %d times; dropping the report and moving on", completeAttempts);
    }
    receivePhase = result == FirmwareResult::OK ? ReceivePhase::RESTARTING : ReceivePhase::IDLE;
}

bool FirmwareUpdateManager::beginSeeding() {
    if (seedPhase != SeedPhase::IDLE) {
        return false;  // a run is already streaming; let it finish rather than restart mid-image
    }

    seedImageLength = firmwareStore->getRunningImageLength();

    FirmwareTrailer trailer{};
    if (!firmwareStore->readRunningTrailer(reinterpret_cast<uint8_t*>(&trailer), sizeof(trailer))) {
        LOG_E(TAG, "beginSeeding: could not read the running image's trailer");
        return false;
    }
    // An unsigned or unprovisioned device must not broadcast garbage
    // credentials: the magic catches an absent/blank trailer, and the
    // length catches one signed for a different build than what's running.
    if (trailer.magic != FIRMWARE_TRAILER_MAGIC) {
        LOG_E(TAG, "beginSeeding: trailer magic mismatch; device is unsigned or unprovisioned");
        return false;
    }
    if (static_cast<size_t>(trailer.imageLength) != seedImageLength) {
        LOG_E(TAG, "beginSeeding: trailer imageLength %u does not match the running image (%zu)",
              trailer.imageLength, seedImageLength);
        return false;
    }

    seedCert = trailer.cert;
    std::memcpy(seedImageSignature, trailer.imageSignature, FIRMWARE_SIG_LENGTH);
    std::memcpy(seedImageHash, cachedRunningImageHash(), FIRMWARE_SHA256_LENGTH);

    seedChunkCount =
        static_cast<uint16_t>((seedImageLength + SEED_CHUNK_SIZE - 1) / SEED_CHUNK_SIZE);
    seedNextChunkIndex = 0;
    // An image with no chunks to stream (nothing this store can serve) has
    // nothing for STREAMING to do, and STREAMING is the one phase that never
    // ends on its own: only a queued chunk moves it on.
    seedPhase = seedChunkCount > 0 ? SeedPhase::STREAMING : SeedPhase::POLLING;

    // A previous run may have left repair state behind; this run starts
    // its own collection from nothing.
    std::memset(repairBitmap, 0, sizeof(repairBitmap));
    haveLastRepairSet = false;
    identicalRepairRounds = 0;
    repairCursor = 0;
    pollWindowTimer.invalidate();

    sendOffer();
    offerTimer.setTimer(SEED_OFFER_INTERVAL_MS);
    return true;
}

void FirmwareUpdateManager::onSendReport(bool success) {
    (void)success;  // any terminal report frees the slot; loss is the repair loop's job, not a retry here
    sendInFlight = false;
}

bool FirmwareUpdateManager::isSeeding() const {
    return seedPhase != SeedPhase::IDLE;
}

void FirmwareUpdateManager::sync() {
    // onOffer only checks isEligible() at accept time; a match starting
    // mid-transfer must not let collection run to a commit-and-restart on a
    // device someone is using. No resume: the seed repeats OFFER for the
    // whole run, so an eligible-again device rejoins at the next one on its
    // own.
    if (receivePhase == ReceivePhase::RECEIVING) {
        if (!isEligible()) {
            LOG_E(TAG, "eligibility lost mid-transfer; aborting the receive");
            abortReceive();
        } else if (receiveDeadlineTimer.expired()) {
            // The seed stopped sending. Without this the phase would hold
            // RECEIVING for the rest of the boot, refusing every later offer
            // while sitting on a partial image and an open write.
            LOG_E(TAG, "no chunk for %lu ms with %u of %u landed; abandoning the receive",
                  RECEIVE_STALL_TIMEOUT_MS, receivedChunkCount, chunkCount);
            abortReceive();
        }
    }

    if (sendInFlight) {
        return;  // at most one kFirmwareUpdate frame in flight, whichever role queued it
    }

    if (receivePhase == ReceivePhase::REPORTING) {
        sendComplete(completeResult);
        return;
    }

    // Ordered after REPORTING, and behind the sendInFlight gate above:
    // COMPLETE must have been handed to the radio and reported on before the
    // device restarts out from under it. No delay of its own — that ordering
    // is the whole guarantee a timer here could offer.
    if (receivePhase == ReceivePhase::RESTARTING) {
        // esp_restart() does not return, so leaving RESTARTING matters only for a
        // store whose restart() does — without it such a store would be restarted
        // every tick.
        receivePhase = ReceivePhase::IDLE;
        firmwareStore->restart();
        return;  // real hardware never returns from this; the fake does, for tests
    }

    if (statusReplyTimer.isRunning() && statusReplyTimer.expired()) {
        sendStatus();
        return;
    }

    if (seedPhase == SeedPhase::IDLE) {
        return;
    }

    if (!offerTimer.isRunning() || offerTimer.expired()) {
        sendOffer();
        offerTimer.setTimer(SEED_OFFER_INTERVAL_MS);
        return;
    }

    if (seedPhase == SeedPhase::STREAMING) {
        sendNextChunk();
        return;
    }

    syncRepair();
}

void FirmwareUpdateManager::syncRepair() {
    if (seedPhase == SeedPhase::POLLING) {
        if (!pollWindowTimer.isRunning()) {
            sendPoll();  // arms the window once the frame is actually queued
            return;
        }
        if (!pollWindowTimer.expired()) {
            return;  // still within the window, collecting STATUS replies
        }
        resolveRepairRound();
        if (seedPhase != SeedPhase::REPAIRING) {
            return;  // resolveRepairRound ended the run: nothing missing, or repair stalled out
        }
    }

    // Skip anything this round's set never asked for (or already sent) in
    // the same call that would otherwise discover there's nothing left —
    // deferring that discovery to a later sync() would leave the round's
    // flags reset one call later than the send that actually finished it.
    while (repairCursor < seedChunkCount && !bitAt(repairStreamSet, repairCursor)) {
        repairCursor++;
    }

    if (repairCursor < seedChunkCount) {
        sendNextRepairChunk();
        return;
    }

    // This round's resends are all out; the next round starts collecting
    // immediately (repairBitmap was already cleared by resolveRepairRound),
    // and its own POLL goes out on the next sync().
    seedPhase = SeedPhase::POLLING;
    pollWindowTimer.invalidate();
    repairCursor = 0;
}

void FirmwareUpdateManager::resolveRepairRound() {
    if (bitmapIsEmpty(repairBitmap)) {
        seedPhase = SeedPhase::IDLE;  // nobody reported a gap: the run is done
        return;
    }

    if (haveLastRepairSet && std::memcmp(repairBitmap, lastRepairSet, sizeof(repairBitmap)) == 0) {
        identicalRepairRounds++;
    } else {
        identicalRepairRounds = 1;
    }
    std::memcpy(lastRepairSet, repairBitmap, sizeof(repairBitmap));
    haveLastRepairSet = true;

    if (identicalRepairRounds >= REPAIR_STALL_ROUNDS) {
        LOG_E(TAG, "repair set unchanged for %d rounds in a row; ending the run", identicalRepairRounds);
        seedPhase = SeedPhase::IDLE;  // waiting out a chunk that never lands would never end the run
        std::memset(repairBitmap, 0, sizeof(repairBitmap));
        return;
    }

    std::memcpy(repairStreamSet, repairBitmap, sizeof(repairBitmap));
    std::memset(repairBitmap, 0, sizeof(repairBitmap));  // the next round starts collecting now
    seedPhase = SeedPhase::REPAIRING;
}

void FirmwareUpdateManager::dispatchSendStatus(const uint8_t* dstMac, const uint8_t* data, size_t length,
                                               bool success, void* ctx) {
    (void)dstMac;
    (void)data;
    (void)length;
    static_cast<FirmwareUpdateManager*>(ctx)->onSendReport(success);
}

void FirmwareUpdateManager::sendOffer() {
    FirmwareOfferPayload offer{};
    offer.command = static_cast<uint8_t>(FirmwareCmd::OFFER);
    std::memcpy(offer.imageSha256, seedImageHash, FIRMWARE_SHA256_LENGTH);
    offer.imageLength = static_cast<uint32_t>(seedImageLength);
    offer.chunkSize = SEED_CHUNK_SIZE;
    offer.chunkCount = seedChunkCount;
    offer.deviceType = static_cast<uint8_t>(deviceType);
    offer.cert = seedCert;
    std::memcpy(offer.imageSignature, seedImageSignature, FIRMWARE_SIG_LENGTH);

    sendFrame(reinterpret_cast<const uint8_t*>(&offer), sizeof(offer), "sendOffer");
}

bool FirmwareUpdateManager::sendFrame(const uint8_t* data, size_t length, const char* what) {
    const int result =
        peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate, data, length);
    if (result != 0) {
        LOG_E(TAG, "%s: sendData refused the frame (%d); retrying next sync()", what, result);
        return false;  // nothing was queued, so no send report will ever arrive for it
    }
    sendInFlight = true;
    return true;
}

bool FirmwareUpdateManager::sendChunkAt(uint16_t index, const char* what) {
    const size_t offset = static_cast<size_t>(index) * SEED_CHUNK_SIZE;
    const uint16_t length =
        static_cast<uint16_t>(std::min<size_t>(SEED_CHUNK_SIZE, seedImageLength - offset));

    uint8_t frame[sizeof(FirmwareChunkHeader) + SEED_CHUNK_SIZE];
    FirmwareChunkHeader* header = reinterpret_cast<FirmwareChunkHeader*>(frame);
    header->command = static_cast<uint8_t>(FirmwareCmd::CHUNK);
    header->index = index;
    header->length = length;

    if (!firmwareStore->readRunningImage(offset, frame + sizeof(FirmwareChunkHeader), length)) {
        LOG_E(TAG, "%s: failed to read the running image at offset %zu", what, offset);
        return false;  // nothing queued; sync() retries this same index next tick
    }
    return sendFrame(frame, sizeof(FirmwareChunkHeader) + length, what);
}

void FirmwareUpdateManager::sendNextChunk() {
    if (!sendChunkAt(seedNextChunkIndex, "sendNextChunk")) {
        return;  // nothing queued: this same index is retried next sync()
    }
    seedNextChunkIndex++;
    if (seedNextChunkIndex >= seedChunkCount) {
        seedPhase = SeedPhase::POLLING;  // every chunk is out; the run moves to POLL/repair
        pollWindowTimer.invalidate();
    }
}

void FirmwareUpdateManager::sendPoll() {
    FirmwarePollPayload poll{};
    poll.command = static_cast<uint8_t>(FirmwareCmd::POLL);
    std::memcpy(poll.imageSha256, seedImageHash, FIRMWARE_SHA256_LENGTH);

    if (!sendFrame(reinterpret_cast<const uint8_t*>(&poll), sizeof(poll), "sendPoll")) {
        return;  // the window stays unarmed: sync() retries this same POLL next tick
    }
    pollWindowTimer.setTimer(SEED_POLL_WINDOW_MS);
}

void FirmwareUpdateManager::sendNextRepairChunk() {
    // syncRepair has already skipped repairCursor forward to a set bit (or
    // to seedChunkCount, in which case it never calls this).
    if (sendChunkAt(repairCursor, "sendNextRepairChunk")) {
        repairCursor++;
    }
}
