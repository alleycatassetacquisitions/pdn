#include "device/firmware-update-manager.hpp"

#include <algorithm>
#include <cstring>

#include "device/drivers/logger.hpp"

namespace {

constexpr const char* TAG = "FirmwareUpdate";

// Every chunk goes out this many times. A receiver that misses chunks, or
// starts mid-run, fills them in from a later pass if one is left.
constexpr uint8_t SEED_PASSES = 3;
// Chunk 0 carries the image's description, so repeating it lets a device that
// becomes eligible mid-run start without waiting for the next pass.
constexpr unsigned long BEACON_INTERVAL_MS = 1000;
// No chunk of the image for this long means its seed has stopped.
constexpr unsigned long RECEIVE_STALL_MS = 5000;

}  // namespace

FirmwareUpdateManager::FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                                             std::function<bool()> isEligible)
    : peerComms(peerComms)
    , firmwareStore(firmwareStore)
    , isEligible(std::move(isEligible)) {
    peerComms->setPacketHandler(
        PktType::kFirmwareUpdate,
        [this](const uint8_t*, const uint8_t* data, size_t length, void*) {
            if (length > sizeof(FirmwareChunkHeader)) {
                FirmwareChunkHeader header{};
                std::memcpy(&header, data, sizeof(header));
                onChunk(header, data + sizeof(header), length - sizeof(header));
            }
        },
        nullptr);
}

FirmwareUpdateManager::~FirmwareUpdateManager() {
    peerComms->clearPacketHandler(PktType::kFirmwareUpdate);
}

void FirmwareUpdateManager::onChunk(const FirmwareChunkHeader& header, const uint8_t* data, size_t length) {
    ImageId id;
    std::memcpy(id.data(), header.imageId, id.size());
    if (receiving) {
        if (id != receiveImageId || header.index >= receiveChunkCount) {
            return;
        }
        // Any chunk of this image shows its seed is still sending, including one
        // already written: a missed chunk comes round only once per pass.
        receiveStallTimer.setTimer(RECEIVE_STALL_MS);
        writeChunk(header.index, data, length);
        return;
    }
    if (refusedQuietTimer.isRunning() && id == refusedImageId) {
        refusedQuietTimer.setTimer(RECEIVE_STALL_MS);
        return;
    }
    if (header.index == 0 && !isSeeding() && isEligible() && startReceiving(header, data, length)) {
        writeChunk(0, data, length);
    }
}

bool FirmwareUpdateManager::startReceiving(const FirmwareChunkHeader& header, const uint8_t* data, size_t length) {
    // Judged before beginWrite erases anything; finishWrite has the final say once the image is whole.
    if (header.imageLength == 0 || !firmwareStore->wantsImage(data, length) ||
        !firmwareStore->beginWrite(header.imageLength)) {
        return false;
    }
    std::memcpy(receiveImageId.data(), header.imageId, receiveImageId.size());
    receiveImageLength = header.imageLength;
    receiveChunkCount = static_cast<uint16_t>((receiveImageLength + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE);
    chunksWritten = 0;
    written.assign(receiveChunkCount, false);
    receiving = true;
    receiveStallTimer.setTimer(RECEIVE_STALL_MS);
    LOG_I(TAG, "receiving a %lu-byte image in %u chunks", static_cast<unsigned long>(receiveImageLength),
          receiveChunkCount);
    return true;
}

void FirmwareUpdateManager::writeChunk(uint16_t index, const uint8_t* data, size_t length) {
    const size_t offset = static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE;
    const size_t expected = std::min<size_t>(FIRMWARE_CHUNK_SIZE, receiveImageLength - offset);
    // Flash bits only clear, so a chunk is written once: a second copy written
    // over the first would corrupt it unless identical.
    if (written[index] || length != expected || !firmwareStore->writeAt(offset, data, length)) {
        return;
    }
    written[index] = true;
    if (++chunksWritten == receiveChunkCount) {
        commit();
    }
}

void FirmwareUpdateManager::commit() {
    receiving = false;
    receiveStallTimer.invalidate();
    if (!firmwareStore->finishWrite()) {
        LOG_E(TAG, "image refused");
        refusedImageId = receiveImageId;
        refusedQuietTimer.setTimer(RECEIVE_STALL_MS);
        return;
    }
    LOG_I(TAG, "image accepted; restarting into it");
    firmwareStore->restart();
}

void FirmwareUpdateManager::stopReceiving() {
    firmwareStore->abortWrite();
    receiving = false;
    receiveStallTimer.invalidate();
}

bool FirmwareUpdateManager::beginSeeding() {
    if (isSeeding()) {
        return false;
    }
    seedImageLength = static_cast<uint32_t>(firmwareStore->getRunningImageLength());
    if (seedImageLength == 0) {
        LOG_E(TAG, "the running image is unsigned or unreadable; nothing to seed");
        return false;
    }
    firmwareStore->getRunningImageId(seedImageId.data());
    seedChunkCount = static_cast<uint16_t>((seedImageLength + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE);
    nextChunk = 0;
    refusedSends = 0;
    passesLeft = SEED_PASSES;
    beaconTimer.setTimer(BEACON_INTERVAL_MS);
    LOG_I(TAG, "seeding a %lu-byte image in %u chunks, %u passes", static_cast<unsigned long>(seedImageLength),
          seedChunkCount, SEED_PASSES);
    return true;
}

bool FirmwareUpdateManager::isSeeding() const {
    return passesLeft != 0;
}

void FirmwareUpdateManager::sync() {
    if (receiving && (!isEligible() || receiveStallTimer.expired())) {
        LOG_W(TAG, "receive dropped with %u of %u chunks", chunksWritten, receiveChunkCount);
        stopReceiving();
    }
    if (refusedQuietTimer.expired()) {
        refusedQuietTimer.invalidate();
    }
    // One frame at a time: the driver's queue empties once the radio has
    // reported on the frame in the air.
    if (!isSeeding() || !peerComms->isSendQueueEmpty()) {
        return;
    }
    if (beaconTimer.expired()) {
        beaconTimer.setTimer(BEACON_INTERVAL_MS);
        sendChunk(0);
        return;
    }
    // A chunk the radio refuses is treated like one lost on the air.
    if (!sendChunk(nextChunk)) {
        refusedSends++;
    }
    if (++nextChunk == seedChunkCount) {
        nextChunk = 0;
        if (--passesLeft == 0) {
            LOG_I(TAG, "run ended after %u passes; the radio refused %lu chunks", SEED_PASSES,
                  static_cast<unsigned long>(refusedSends));
        }
    }
}

bool FirmwareUpdateManager::sendChunk(uint16_t index) {
    uint8_t frame[sizeof(FirmwareChunkHeader) + FIRMWARE_CHUNK_SIZE];
    FirmwareChunkHeader header{};
    std::memcpy(header.imageId, seedImageId.data(), seedImageId.size());
    header.imageLength = seedImageLength;
    header.index = index;
    std::memcpy(frame, &header, sizeof(header));
    const size_t offset = static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE;
    const size_t length = std::min<size_t>(FIRMWARE_CHUNK_SIZE, seedImageLength - offset);
    return firmwareStore->readRunningImage(offset, frame + sizeof(header), length) &&
           peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate, frame,
                               sizeof(header) + length) == 0;
}
