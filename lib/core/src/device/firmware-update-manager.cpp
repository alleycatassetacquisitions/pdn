#include "device/firmware-update-manager.hpp"

#include <algorithm>
#include <cstring>

#include "device/drivers/logger.hpp"

namespace {

constexpr const char* TAG = "FirmwareUpdate";

// Every chunk goes out this many times. A receiver that misses chunks in one
// pass, or joins mid-run, fills them in from a later one; nobody reports back.
constexpr uint8_t SEED_PASSES = 3;
constexpr unsigned long OFFER_INTERVAL_MS = 1000;
constexpr unsigned long SEED_SEND_STALL_MS = 5000;
// No chunk of the image for this long means the seed has stopped.
constexpr unsigned long RECEIVE_STALL_MS = 5000;

}  // namespace

FirmwareUpdateManager::FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                                             std::function<bool()> isEligible)
    : peerComms(peerComms)
    , firmwareStore(firmwareStore)
    , isEligible(std::move(isEligible)) {
    peerComms->setPacketHandler(PktType::kFirmwareUpdate, &FirmwareUpdateManager::dispatchPacket, this);
    peerComms->setSendStatusHandler(PktType::kFirmwareUpdate, &FirmwareUpdateManager::dispatchSendStatus, this);
}

FirmwareUpdateManager::~FirmwareUpdateManager() {
    peerComms->clearPacketHandler(PktType::kFirmwareUpdate);
    peerComms->clearSendStatusHandler(PktType::kFirmwareUpdate);
}

void FirmwareUpdateManager::dispatchPacket(const uint8_t* src, const uint8_t* data, size_t length, void* ctx) {
    static_cast<FirmwareUpdateManager*>(ctx)->onPacketReceived(src, data, length);
}

void FirmwareUpdateManager::dispatchSendStatus(const uint8_t* dstMac, const uint8_t* data, size_t length,
                                               bool success, void* ctx) {
    (void)dstMac;
    (void)data;
    (void)length;
    static_cast<FirmwareUpdateManager*>(ctx)->onSendReport(success);
}

void FirmwareUpdateManager::onPacketReceived(const uint8_t* src, const uint8_t* data, size_t length) {
    (void)src;
    if (length == 0) {
        return;
    }
    if (data[0] == static_cast<uint8_t>(FirmwareCmd::OFFER) && length == sizeof(FirmwareOfferPayload)) {
        FirmwareOfferPayload offer{};
        std::memcpy(&offer, data, sizeof(offer));
        onOffer(offer);
    } else if (data[0] == static_cast<uint8_t>(FirmwareCmd::CHUNK) && length > sizeof(FirmwareChunkHeader)) {
        FirmwareChunkHeader header{};
        std::memcpy(&header, data, sizeof(header));
        onChunk(header, data + sizeof(header), length - sizeof(header));
    }
}

void FirmwareUpdateManager::onOffer(const FirmwareOfferPayload& offer) {
    if (receiving || seeding || !isEligible()) {
        return;
    }
    uint8_t runningId[FIRMWARE_IMAGE_ID_LENGTH];
    firmwareStore->getRunningImageId(runningId);
    if (std::memcmp(offer.imageId, runningId, FIRMWARE_IMAGE_ID_LENGTH) == 0 ||
        std::memcmp(offer.imageId, refusedImageId, FIRMWARE_IMAGE_ID_LENGTH) == 0) {
        return;
    }
    const uint32_t expectedChunks = (offer.imageLength + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE;
    if (offer.imageLength == 0 || offer.chunkCount != expectedChunks || offer.chunkCount > MAX_CHUNKS ||
        offer.imageLength > firmwareStore->getInactiveSlotSize()) {
        LOG_W(TAG, "offer for a %lu-byte image in %u chunks does not fit; ignored",
              static_cast<unsigned long>(offer.imageLength), offer.chunkCount);
        return;
    }
    if (!firmwareStore->beginWrite(offer.imageLength)) {
        return;
    }
    std::memcpy(receiveImageId, offer.imageId, FIRMWARE_IMAGE_ID_LENGTH);
    receiveImageLength = offer.imageLength;
    receiveChunkCount = offer.chunkCount;
    chunksWritten = 0;
    written.reset();
    receiving = true;
    receiveStallTimer.setTimer(RECEIVE_STALL_MS);
    LOG_I(TAG, "receiving a %lu-byte image in %u chunks", static_cast<unsigned long>(receiveImageLength),
          receiveChunkCount);
}

void FirmwareUpdateManager::onChunk(const FirmwareChunkHeader& header, const uint8_t* data, size_t length) {
    if (!receiving || std::memcmp(header.imageIdPrefix, receiveImageId, sizeof(header.imageIdPrefix)) != 0 ||
        header.index >= receiveChunkCount) {
        return;
    }
    // Any chunk of this image shows the seed is still sending, including one
    // already written: a missed chunk comes round only once per pass.
    receiveStallTimer.setTimer(RECEIVE_STALL_MS);
    if (written.test(header.index)) {
        return;
    }
    const size_t offset = static_cast<size_t>(header.index) * FIRMWARE_CHUNK_SIZE;
    const size_t expected = std::min<size_t>(FIRMWARE_CHUNK_SIZE, receiveImageLength - offset);
    if (length != expected || !firmwareStore->writeAt(offset, data, length)) {
        return;
    }
    // Flash bits only clear, so a chunk is written once: a second copy written
    // over the first would corrupt it unless identical.
    written.set(header.index);
    chunksWritten++;
    if (chunksWritten == receiveChunkCount) {
        commit();
    }
}

void FirmwareUpdateManager::commit() {
    receiving = false;
    receiveStallTimer.invalidate();
    if (!firmwareStore->finishWrite() || !firmwareStore->setBootToWritten()) {
        LOG_E(TAG, "image refused");
        std::memcpy(refusedImageId, receiveImageId, FIRMWARE_IMAGE_ID_LENGTH);
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
    if (seeding) {
        return false;
    }
    seedImageLength = static_cast<uint32_t>(firmwareStore->getRunningImageLength());
    if (seedImageLength == 0) {
        LOG_E(TAG, "no running image to seed");
        return false;
    }
    firmwareStore->getRunningImageId(seedImageId);
    seedChunkCount = static_cast<uint16_t>((seedImageLength + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE);
    nextChunk = 0;
    passesLeft = SEED_PASSES;
    seeding = true;
    offerTimer.setTimer(0);
    sendStallTimer.setTimer(SEED_SEND_STALL_MS);
    LOG_I(TAG, "seeding a %lu-byte image in %u chunks, %u passes", static_cast<unsigned long>(seedImageLength),
          seedChunkCount, SEED_PASSES);
    return true;
}

bool FirmwareUpdateManager::isSeeding() const {
    return seeding;
}

bool FirmwareUpdateManager::isReceiving() const {
    return receiving;
}

void FirmwareUpdateManager::onSendReport(bool success) {
    (void)success;  // no frame is resent: a lost chunk comes round again next pass
    sendInFlight = false;
}

void FirmwareUpdateManager::sync() {
    if (receiving && (!isEligible() || receiveStallTimer.expired())) {
        LOG_W(TAG, "receive dropped with %u of %u chunks", chunksWritten, receiveChunkCount);
        stopReceiving();
    }
    if (!seeding || sendInFlight) {
        return;
    }
    if (sendStallTimer.expired()) {
        LOG_E(TAG, "the radio refused every frame for %lu ms; run abandoned", SEED_SEND_STALL_MS);
        seeding = false;
        return;
    }
    if (offerTimer.expired()) {
        sendOffer();
        offerTimer.setTimer(OFFER_INTERVAL_MS);
        return;
    }
    if (!sendChunk(nextChunk)) {
        return;
    }
    if (++nextChunk == seedChunkCount) {
        nextChunk = 0;
        if (--passesLeft == 0) {
            LOG_I(TAG, "run ended after %u passes", SEED_PASSES);
            seeding = false;
        }
    }
}

void FirmwareUpdateManager::sendOffer() {
    FirmwareOfferPayload offer{};
    offer.command = static_cast<uint8_t>(FirmwareCmd::OFFER);
    std::memcpy(offer.imageId, seedImageId, FIRMWARE_IMAGE_ID_LENGTH);
    offer.imageLength = seedImageLength;
    offer.chunkCount = seedChunkCount;
    sendFrame(reinterpret_cast<const uint8_t*>(&offer), sizeof(offer));
}

bool FirmwareUpdateManager::sendChunk(uint16_t index) {
    uint8_t frame[sizeof(FirmwareChunkHeader) + FIRMWARE_CHUNK_SIZE];
    FirmwareChunkHeader header{};
    header.command = static_cast<uint8_t>(FirmwareCmd::CHUNK);
    std::memcpy(header.imageIdPrefix, seedImageId, sizeof(header.imageIdPrefix));
    header.index = index;
    std::memcpy(frame, &header, sizeof(header));
    const size_t offset = static_cast<size_t>(index) * FIRMWARE_CHUNK_SIZE;
    const size_t length = std::min<size_t>(FIRMWARE_CHUNK_SIZE, seedImageLength - offset);
    if (!firmwareStore->readRunningImage(offset, frame + sizeof(header), length)) {
        LOG_E(TAG, "could not read chunk %u of the running image", index);
        return false;
    }
    return sendFrame(frame, sizeof(header) + length);
}

bool FirmwareUpdateManager::sendFrame(const uint8_t* data, size_t length) {
    // Latched before the call: the driver may report completion before
    // sendData returns, and setting it afterwards would lose that report.
    sendInFlight = true;
    if (peerComms->sendData(peerComms->getGlobalBroadcastAddress(), PktType::kFirmwareUpdate, data, length) != 0) {
        sendInFlight = false;
        return false;
    }
    sendStallTimer.setTimer(SEED_SEND_STALL_MS);
    return true;
}
