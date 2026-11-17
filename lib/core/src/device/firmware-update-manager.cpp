#include "device/firmware-update-manager.hpp"

#include <algorithm>
#include <cstring>

#include "device/drivers/logger.hpp"

namespace {

constexpr const char* TAG = "FirmwareUpdate";

// A receiver fills chunks it missed, or started after, from a later pass.
constexpr uint8_t SEED_PASSES = 3;
// Chunk 0 carries the app description, so a receiver joining mid-pass can start at once.
constexpr unsigned long BEACON_INTERVAL_MS = 1000;
constexpr unsigned long RECEIVE_STALL_MS = 5000;

uint16_t chunkCount(uint32_t imageLength) {
    return static_cast<uint16_t>((imageLength + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE);
}

size_t chunkLength(uint32_t imageLength, uint16_t index) {
    return std::min<size_t>(FIRMWARE_CHUNK_SIZE, imageLength - static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE);
}

}  // namespace

FirmwareUpdateManager::FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore)
    : peerComms(peerComms)
    , firmwareStore(firmwareStore) {
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
        // Already-written chunks still prove the seed is alive; a missed one returns only next pass.
        quietTimer.setTimer(RECEIVE_STALL_MS);
        if (receive == Receive::WRITING) {
            writeChunk(header.index, data, length);
        }
        return;
    }
    // A seed streams its own image and never takes one.
    if (receive != Receive::WRITING && header.index == 0 && eligible && !isSeeding() &&
        startReceiving(header, data, length)) {
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
    // Flash bits only clear: a later copy that differs (a forged one) would corrupt the first.
    if (written[index] || length != chunkLength(receiveImageLength, index)) {
        return;
    }
    // Chunk 0 holds the image header, so it goes to flash last: a reset mid-receive leaves nothing bootable.
    if (index == 0) {
        chunkZero.assign(data, data + length);
    } else if (!firmwareStore->writeAt(static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE, data, length)) {
        return;
    }
    written[index] = true;
    chunksWritten++;
}

void FirmwareUpdateManager::commit() {
    if (!firmwareStore->writeAt(0, chunkZero.data(), chunkZero.size())) {
        stopReceiving();
        return;
    }
    if (!firmwareStore->finishWrite()) {
        LOG_E(TAG, "image refused");
        receive = Receive::REFUSED;
        return;
    }
    LOG_I(TAG, "image accepted; restarting into it");
    receive = Receive::NONE;
    firmwareStore->restart();
}

void FirmwareUpdateManager::stopReceiving() {
    firmwareStore->abortWrite();
    receive = Receive::NONE;
}

bool FirmwareUpdateManager::beginSeeding() {
    seedImageLength = static_cast<uint32_t>(firmwareStore->getRunningImageLength());
    if (seedImageLength == 0) {
        LOG_E(TAG, "the running image is unreadable; nothing to seed");
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

void FirmwareUpdateManager::setEligible(bool eligible) {
    this->eligible = eligible;
    if (!eligible && receive == Receive::WRITING) {
        stopReceiving();
    }
}

bool FirmwareUpdateManager::isSeeding() const {
    return passesLeft != 0;
}

void FirmwareUpdateManager::sync() {
    if (receive == Receive::WRITING && quietTimer.expired()) {
        LOG_W(TAG, "receive dropped with %u of %u chunks", chunksWritten, receiveChunkCount);
        stopReceiving();
    } else if (receive == Receive::WRITING && chunksWritten == receiveChunkCount) {
        // Here rather than on the last chunk, so a button press in the same pass is seen first.
        commit();
    } else if (receive == Receive::REFUSED && quietTimer.expired()) {
        receive = Receive::NONE;
    }
    if (!isSeeding() || !peerComms->isSendQueueEmpty()) {
        return;
    }
    if (beaconTimer.expired()) {
        beaconTimer.setTimer(BEACON_INTERVAL_MS);
        sendChunk(0);
        return;
    }
    // A chunk that could not be read or was refused counts as lost on the air.
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
