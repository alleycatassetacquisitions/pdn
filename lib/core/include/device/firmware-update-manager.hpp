#pragma once

#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "device/drivers/firmware-store-interface.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "utils/simple-timer.hpp"

/**
 * Moves a firmware image between devices over ESP-NOW broadcast. A seed
 * streams the image it is running, every chunk SEED_PASSES times, and listens
 * to nobody; a receiver writes each chunk the first time it hears it and, once
 * it has all of them, hands the image to the store, which accepts it only if
 * IDF finds it intact, signed by a key the running image was signed with, and
 * newer than the running image.
 */
class FirmwareUpdateManager {
public:
    /**
     * isEligible is polled before taking an offer and on every sync() while
     * receiving; a receive in progress is dropped the moment it reads false.
     */
    FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                          std::function<bool()> isEligible);
    ~FirmwareUpdateManager();

    /** Starts streaming this device's running image; false if there is none to stream. */
    bool beginSeeding();
    /** True until the seed's last pass has been sent. */
    bool isSeeding() const;
    /** True while an image is being collected. */
    bool isReceiving() const;
    /** Call every loop pass, on every device: drives the seed and times out receives. */
    void sync();

    /** Packet handler for PktType::kFirmwareUpdate. */
    void onPacketReceived(const uint8_t* src, const uint8_t* data, size_t length);
    /** Send-status handler for PktType::kFirmwareUpdate: frees the one send slot. */
    void onSendReport(bool success);

private:
    static constexpr size_t MAX_CHUNKS = OTA_PARTITION_SIZE / FIRMWARE_CHUNK_SIZE + 1;

    static void dispatchPacket(const uint8_t* src, const uint8_t* data, size_t length, void* ctx);
    static void dispatchSendStatus(const uint8_t* dstMac, const uint8_t* data, size_t length, bool success,
                                   void* ctx);

    void onOffer(const FirmwareOfferPayload& offer);
    void onChunk(const FirmwareChunkHeader& header, const uint8_t* data, size_t length);
    void commit();
    void stopReceiving();
    void sendOffer();
    bool sendChunk(uint16_t index);
    bool sendFrame(const uint8_t* data, size_t length);

    PeerCommsInterface* peerComms;
    FirmwareStoreInterface* firmwareStore;
    std::function<bool()> isEligible;

    // Seed.
    bool seeding = false;
    uint8_t seedImageId[FIRMWARE_IMAGE_ID_LENGTH] = {};
    uint32_t seedImageLength = 0;
    uint16_t seedChunkCount = 0;
    uint16_t nextChunk = 0;
    uint8_t passesLeft = 0;
    SimpleTimer offerTimer;
    // Restarted by every frame the radio accepts; expired means the radio has
    // refused everything for SEED_SEND_STALL_MS and the run is abandoned.
    SimpleTimer sendStallTimer;
    // Cleared by onSendReport, which the ESP-NOW driver calls from the WiFi task.
    std::atomic<bool> sendInFlight{false};

    // Receiver.
    bool receiving = false;
    uint8_t receiveImageId[FIRMWARE_IMAGE_ID_LENGTH] = {};
    uint32_t receiveImageLength = 0;
    uint16_t receiveChunkCount = 0;
    uint16_t chunksWritten = 0;
    std::bitset<MAX_CHUNKS> written;
    SimpleTimer receiveStallTimer;
    // The last image the store refused, so a seed still streaming it is not
    // downloaded again.
    uint8_t refusedImageId[FIRMWARE_IMAGE_ID_LENGTH] = {};
};
