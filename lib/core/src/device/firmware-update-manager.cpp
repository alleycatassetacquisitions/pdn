#include "device/firmware-update-manager.hpp"

#include "device/firmware-verify.hpp"
#include "device/drivers/logger.hpp"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

const char* TAG = "FirmwareUpdate";

// Bytes per chunk the seed streams at. Fixed rather than negotiated: the
// build-time signer signs imageSha256|imageLength|chunkSize|chunkCount, so
// the device must reproduce the exact chunkSize/chunkCount the signature
// covers rather than choosing one at runtime.
constexpr uint16_t SEED_CHUNK_SIZE = 1400;

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

// Hashes the running image by streaming it through flash in fixed-size
// chunks rather than buffering the whole thing: a running image can be
// several megabytes. Called at most once per boot (the caller caches the
// result), so the cost here is not a per-OFFER concern.
void hashRunningImage(FirmwareStoreInterface* firmwareStore, uint8_t out[FIRMWARE_SHA256_LENGTH]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);

    uint8_t buffer[256];
    const size_t total = firmwareStore->getRunningImageLength();
    size_t offset = 0;
    while (offset < total) {
        const size_t chunk = std::min(sizeof(buffer), total - offset);
        if (!firmwareStore->readRunningImage(offset, buffer, chunk)) {
            LOG_E(TAG, "readRunningImage failed at offset %zu of %zu; running-image hash is incomplete",
                  offset, total);
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
                                             const uint8_t* rootPublicKey)
    : peerComms(peerComms)
    , firmwareStore(firmwareStore)
    , rootPublicKey(rootPublicKey) {
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
        hashRunningImage(firmwareStore, runningImageHash);
        runningImageHashComputed = true;
    }
    return runningImageHash;
}

void FirmwareUpdateManager::onOffer(const uint8_t* fromMac, const FirmwareOfferPayload& offer) {
    (void)fromMac;  // nothing unicasts a reply to the seed yet

    if (receiving && std::memcmp(offer.imageSha256, currentImageHash, FIRMWARE_SHA256_LENGTH) == 0) {
        return;  // the seed repeats OFFER once a second; don't reopen a transfer in flight
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

    if (receiving) {
        firmwareStore->abortWrite();  // switching images: don't leave the old write open
    }
    if (!firmwareStore->beginWrite(offer.imageLength)) {
        LOG_E(TAG, "beginWrite failed for a %u byte image", offer.imageLength);
        receiving = false;  // fail closed: the slot may be left partially erased
        return;
    }

    std::memcpy(currentImageHash, offer.imageSha256, FIRMWARE_SHA256_LENGTH);
    imageLength = offer.imageLength;
    chunkCount = offer.chunkCount;
    chunkSize = offer.chunkSize;
    receivedChunkCount = 0;
    std::memset(bitmap, 0, sizeof(bitmap));
    receiving = true;
}

void FirmwareUpdateManager::onChunk(const FirmwareChunkHeader& header, const uint8_t* data) {
    if (!receiving || header.index >= chunkCount || bitmapBit(header.index)) {
        return;
    }

    const size_t offset = static_cast<size_t>(header.index) * chunkSize;
    if (offset > imageLength || header.length > imageLength - offset) {
        LOG_D(TAG, "chunk %u would write past the declared image length", header.index);
        return;
    }

    if (!firmwareStore->writeAt(offset, data, header.length)) {
        LOG_D(TAG, "writeAt failed for chunk %u", header.index);
        return;
    }

    setBitmapBit(header.index);
    receivedChunkCount++;
}

void FirmwareUpdateManager::onPoll(const FirmwarePollPayload& poll) {
    if (!receiving) {
        return;  // nothing to report on
    }
    if (std::memcmp(poll.imageSha256, currentImageHash, FIRMWARE_SHA256_LENGTH) != 0) {
        return;  // a poll for an image other than the one being collected
    }
    // Re-rolled on every poll heard, including a repeat: nothing here tracks
    // whether a reply is already pending, and a fresh roll is no worse than
    // whatever delay was already running.
    statusReplyTimer.setTimer(static_cast<unsigned long>(std::rand() % STATUS_BACKOFF_CEILING_MS));
}

void FirmwareUpdateManager::onStatus(const FirmwareStatusPayload& status) {
    if (!seeding) {
        return;  // no run in progress for a report to apply to
    }
    if (std::memcmp(status.imageSha256, cachedRunningImageHash(), FIRMWARE_SHA256_LENGTH) != 0) {
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
    return receiving;
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

    const int result = peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate,
                                           reinterpret_cast<const uint8_t*>(&status), sizeof(status));
    if (result != 0) {
        LOG_E(TAG, "sendStatus: sendData refused the frame (%d); retrying next sync()", result);
        return;  // statusReplyTimer stays expired: sync() retries this same reply next tick
    }
    statusReplyTimer.invalidate();
    sendInFlight = true;
}

bool FirmwareUpdateManager::beginSeeding() {
    if (seeding) {
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

    seedChunkCount =
        static_cast<uint16_t>((seedImageLength + SEED_CHUNK_SIZE - 1) / SEED_CHUNK_SIZE);
    seedNextChunkIndex = 0;
    seeding = true;

    // A previous run may have left repair state behind; this run starts
    // its own collection from nothing.
    std::memset(repairBitmap, 0, sizeof(repairBitmap));
    haveLastRepairSet = false;
    identicalRepairRounds = 0;
    pollSentForRound = false;
    repairRoundReady = false;
    repairCursor = 0;

    sendOffer();
    offerTimer.setTimer(SEED_OFFER_INTERVAL_MS);
    return true;
}

void FirmwareUpdateManager::onSendReport(bool success) {
    (void)success;  // any terminal report frees the slot; loss is the repair loop's job, not a retry here
    sendInFlight = false;
}

bool FirmwareUpdateManager::isSeeding() const {
    return seeding;
}

void FirmwareUpdateManager::sync() {
    if (sendInFlight) {
        return;  // at most one kFirmwareUpdate frame in flight, whichever role queued it
    }

    if (statusReplyTimer.isRunning() && statusReplyTimer.expired()) {
        sendStatus();
        return;
    }

    if (!seeding) {
        return;
    }

    if (!offerTimer.isRunning() || offerTimer.expired()) {
        sendOffer();
        offerTimer.setTimer(SEED_OFFER_INTERVAL_MS);
        return;
    }

    if (seedNextChunkIndex < seedChunkCount) {
        sendNextChunk();
        return;
    }

    syncRepair();
}

void FirmwareUpdateManager::syncRepair() {
    if (!pollSentForRound) {
        sendPoll();
        return;
    }

    if (!repairRoundReady) {
        if (!pollWindowTimer.expired()) {
            return;  // still within the window, collecting STATUS replies
        }
        resolveRepairRound();
        if (!seeding) {
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
    pollSentForRound = false;
    repairRoundReady = false;
    repairCursor = 0;
}

void FirmwareUpdateManager::resolveRepairRound() {
    if (bitmapIsEmpty(repairBitmap)) {
        seeding = false;  // nobody reported a gap: the run is done
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
        seeding = false;  // waiting out a chunk that never lands would never end the run
        std::memset(repairBitmap, 0, sizeof(repairBitmap));
        return;
    }

    std::memcpy(repairStreamSet, repairBitmap, sizeof(repairBitmap));
    std::memset(repairBitmap, 0, sizeof(repairBitmap));  // the next round starts collecting now
    repairRoundReady = true;
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
    std::memcpy(offer.imageSha256, cachedRunningImageHash(), FIRMWARE_SHA256_LENGTH);
    offer.imageLength = static_cast<uint32_t>(seedImageLength);
    offer.chunkSize = SEED_CHUNK_SIZE;
    offer.chunkCount = seedChunkCount;
    offer.cert = seedCert;
    std::memcpy(offer.imageSignature, seedImageSignature, FIRMWARE_SIG_LENGTH);

    const int result = peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate,
                                           reinterpret_cast<const uint8_t*>(&offer), sizeof(offer));
    if (result != 0) {
        LOG_E(TAG, "sendOffer: sendData refused the frame (%d); retrying next sync()", result);
        return;  // sendInFlight stays false: nothing was queued, so no report will ever arrive for it
    }
    sendInFlight = true;
}

void FirmwareUpdateManager::sendNextChunk() {
    const uint16_t index = seedNextChunkIndex;
    const size_t offset = static_cast<size_t>(index) * SEED_CHUNK_SIZE;
    const uint16_t length =
        static_cast<uint16_t>(std::min<size_t>(SEED_CHUNK_SIZE, seedImageLength - offset));

    uint8_t frame[sizeof(FirmwareChunkHeader) + SEED_CHUNK_SIZE];
    FirmwareChunkHeader* header = reinterpret_cast<FirmwareChunkHeader*>(frame);
    header->command = static_cast<uint8_t>(FirmwareCmd::CHUNK);
    header->index = index;
    header->length = length;

    if (!firmwareStore->readRunningImage(offset, frame + sizeof(FirmwareChunkHeader), length)) {
        LOG_E(TAG, "sendNextChunk: failed to read the running image at offset %zu", offset);
        return;  // sendInFlight stays false; sync() retries this same index next tick
    }

    const int result = peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate, frame,
                                           sizeof(FirmwareChunkHeader) + length);
    if (result != 0) {
        LOG_E(TAG, "sendNextChunk: sendData refused chunk %u (%d); retrying next sync()", index, result);
        return;  // seedNextChunkIndex not advanced, sendInFlight stays false: nothing was queued
    }
    seedNextChunkIndex++;
    sendInFlight = true;
}

void FirmwareUpdateManager::sendPoll() {
    FirmwarePollPayload poll{};
    poll.command = static_cast<uint8_t>(FirmwareCmd::POLL);
    std::memcpy(poll.imageSha256, cachedRunningImageHash(), FIRMWARE_SHA256_LENGTH);

    const int result = peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate,
                                           reinterpret_cast<const uint8_t*>(&poll), sizeof(poll));
    if (result != 0) {
        LOG_E(TAG, "sendPoll: sendData refused the frame (%d); retrying next sync()", result);
        return;  // pollSentForRound stays false: sync() retries next tick
    }
    sendInFlight = true;
    pollSentForRound = true;
    pollWindowTimer.setTimer(SEED_POLL_WINDOW_MS);
}

void FirmwareUpdateManager::sendNextRepairChunk() {
    // syncRepair has already skipped repairCursor forward to a set bit (or
    // to seedChunkCount, in which case it never calls this).
    const uint16_t index = repairCursor;
    const size_t offset = static_cast<size_t>(index) * SEED_CHUNK_SIZE;
    const uint16_t length =
        static_cast<uint16_t>(std::min<size_t>(SEED_CHUNK_SIZE, seedImageLength - offset));

    uint8_t frame[sizeof(FirmwareChunkHeader) + SEED_CHUNK_SIZE];
    FirmwareChunkHeader* header = reinterpret_cast<FirmwareChunkHeader*>(frame);
    header->command = static_cast<uint8_t>(FirmwareCmd::CHUNK);
    header->index = index;
    header->length = length;

    if (!firmwareStore->readRunningImage(offset, frame + sizeof(FirmwareChunkHeader), length)) {
        LOG_E(TAG, "sendNextRepairChunk: failed to read the running image at offset %zu", offset);
        return;  // repairCursor not advanced; sync() retries this same index next tick
    }

    const int result = peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate, frame,
                                           sizeof(FirmwareChunkHeader) + length);
    if (result != 0) {
        LOG_E(TAG, "sendNextRepairChunk: sendData refused chunk %u (%d); retrying next sync()", index, result);
        return;  // repairCursor not advanced, sendInFlight stays false: nothing was queued
    }
    repairCursor = index + 1;
    sendInFlight = true;
}
