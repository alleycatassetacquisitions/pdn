#include "device/firmware-update-manager.hpp"

#include "device/firmware-verify.hpp"
#include "device/drivers/logger.hpp"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>

namespace {

const char* TAG = "FirmwareUpdate";

// Hashes the running image by streaming it through flash in fixed-size
// chunks rather than buffering the whole thing: a running image can be
// several megabytes, and this runs on every OFFER received.
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
                LOG_E(TAG, "CHUNK frame too short (%zu < %zu)", length, sizeof(FirmwareChunkHeader));
                return;
            }
            const FirmwareChunkHeader* header = reinterpret_cast<const FirmwareChunkHeader*>(data);
            const uint8_t* body = data + sizeof(FirmwareChunkHeader);
            const size_t bodyLength = length - sizeof(FirmwareChunkHeader);
            if (static_cast<size_t>(header->length) > bodyLength) {
                LOG_E(TAG, "CHUNK declares %u bytes, frame carries %zu", header->length, bodyLength);
                return;
            }
            onChunk(*header, body);
            return;
        }
        default:
            return;  // POLL/STATUS/COMPLETE belong to later tasks
    }
}

void FirmwareUpdateManager::onOffer(const uint8_t* fromMac, const FirmwareOfferPayload& offer) {
    (void)fromMac;  // no per-seed routing yet; the repair loop task needs it, not this one

    if (receiving && std::memcmp(offer.imageSha256, currentImageHash, FIRMWARE_SHA256_LENGTH) == 0) {
        return;  // the seed repeats OFFER once a second; don't reopen a transfer in flight
    }

    uint8_t runningHash[FIRMWARE_SHA256_LENGTH];
    hashRunningImage(firmwareStore, runningHash);
    if (std::memcmp(offer.imageSha256, runningHash, FIRMWARE_SHA256_LENGTH) == 0) {
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
        return;
    }

    std::memcpy(currentImageHash, offer.imageSha256, FIRMWARE_SHA256_LENGTH);
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
    if (!firmwareStore->writeAt(offset, data, header.length)) {
        LOG_E(TAG, "writeAt failed for chunk %u", header.index);
        return;
    }

    setBitmapBit(header.index);
    receivedChunkCount++;
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
