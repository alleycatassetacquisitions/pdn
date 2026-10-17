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

// The seed and the receiver both cut an image here.
uint16_t chunkCount(uint32_t imageLength) {
    return static_cast<uint16_t>((imageLength + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE);
}

size_t chunkLength(uint32_t imageLength, uint16_t index) {
    return std::min<size_t>(FIRMWARE_CHUNK_SIZE, imageLength - static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE);
}

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
    if (receive != Receive::NONE && id == receiveImageId && header.index < receiveChunkCount) {
        // Any chunk of this image shows its seed is still sending, including one
        // already written: a missed chunk comes round only once per pass.
        quietTimer.setTimer(RECEIVE_STALL_MS);
        if (receive == Receive::REFUSED) {
            return;
        }
        // Checked here as well as in sync(): this chunk could be the last one, and
        // committing it restarts the device before sync() runs again.
        if (!mayReceive()) {
            stopReceiving();
            return;
        }
        writeChunk(header.index, data, length);
        return;
    }
    if (receive != Receive::WRITING && header.index == 0 && mayReceive() && startReceiving(header, data, length)) {
        writeChunk(0, data, length);
    }
}

bool FirmwareUpdateManager::startReceiving(const FirmwareChunkHeader& header, const uint8_t* data, size_t length) {
    if (!firmwareStore->beginWrite(data, length, header.imageLength)) {
        return false;
    }
    std::memcpy(receiveImageId.data(), header.imageId, receiveImageId.size());
    receiveImageLength = header.imageLength;
    receiveChunkCount = chunkCount(receiveImageLength);
    chunksWritten = 0;
    written.assign(receiveChunkCount, false);
    receive = Receive::WRITING;
    quietTimer.setTimer(RECEIVE_STALL_MS);
    LOG_I(TAG, "receiving a %lu-byte image in %u chunks", static_cast<unsigned long>(receiveImageLength),
          receiveChunkCount);
    return true;
}

void FirmwareUpdateManager::writeChunk(uint16_t index, const uint8_t* data, size_t length) {
    const size_t offset = static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE;
    // Flash bits only clear, so a chunk is written once: a second copy written
    // over the first would corrupt it unless identical.
    if (written[index] || length != chunkLength(receiveImageLength, index) ||
        !firmwareStore->writeAt(offset, data, length)) {
        return;
    }
    written[index] = true;
    if (++chunksWritten == receiveChunkCount) {
        commit();
    }
}

void FirmwareUpdateManager::commit() {
    if (!firmwareStore->finishWrite()) {
        LOG_E(TAG, "image refused");
        receive = Receive::REFUSED;
        return;
    }
    LOG_I(TAG, "image accepted; restarting into it");
    firmwareStore->restart();
}

void FirmwareUpdateManager::stopReceiving() {
    firmwareStore->abortWrite();
    receive = Receive::NONE;
}

bool FirmwareUpdateManager::beginSeeding() {
    seedImageLength = static_cast<uint32_t>(firmwareStore->getRunningImageLength());
    if (seedImageLength == 0) {
        LOG_E(TAG, "the running image is unsigned or unreadable; nothing to seed");
        return false;
    }
    firmwareStore->getRunningImageId(seedImageId.data());
    seedChunkCount = chunkCount(seedImageLength);
    nextChunk = 0;
    passesLeft = SEED_PASSES;
    beaconTimer.setTimer(BEACON_INTERVAL_MS);
    LOG_I(TAG, "seeding a %lu-byte image in %u chunks, %u passes", static_cast<unsigned long>(seedImageLength),
          seedChunkCount, SEED_PASSES);
    return true;
}

bool FirmwareUpdateManager::mayReceive() const {
    // A seed streams its own image and never takes one.
    return !isSeeding() && isEligible();
}

bool FirmwareUpdateManager::isSeeding() const {
    return passesLeft != 0;
}

void FirmwareUpdateManager::sync() {
    if (receive == Receive::WRITING && (!mayReceive() || quietTimer.expired())) {
        LOG_W(TAG, "receive dropped with %u of %u chunks", chunksWritten, receiveChunkCount);
        stopReceiving();
    } else if (receive == Receive::REFUSED && quietTimer.expired()) {
        receive = Receive::NONE;
    }
    // One frame at a time: the driver's queue empties once the frame in the air
    // has been sent or given up on.
    if (!isSeeding() || !peerComms->isSendQueueEmpty()) {
        return;
    }
    if (beaconTimer.expired()) {
        beaconTimer.setTimer(BEACON_INTERVAL_MS);
        sendChunk(0);
        return;
    }
    // A chunk that could not be read or that the radio refused counts as lost on
    // the air.
    sendChunk(nextChunk);
    if (++nextChunk == seedChunkCount) {
        nextChunk = 0;
        if (--passesLeft == 0) {
            LOG_I(TAG, "run ended after %u passes", SEED_PASSES);
        }
    }
}

void FirmwareUpdateManager::sendChunk(uint16_t index) {
    uint8_t frame[sizeof(FirmwareChunkHeader) + FIRMWARE_CHUNK_SIZE];
    FirmwareChunkHeader header{};
    std::memcpy(header.imageId, seedImageId.data(), seedImageId.size());
    header.imageLength = seedImageLength;
    header.index = index;
    std::memcpy(frame, &header, sizeof(header));
    const size_t offset = static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE;
    const size_t length = chunkLength(seedImageLength, index);
    if (firmwareStore->readRunningImage(offset, frame + sizeof(header), length)) {
        peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate, frame,
                            sizeof(header) + length);
    }
}
