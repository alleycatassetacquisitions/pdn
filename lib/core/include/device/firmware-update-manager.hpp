#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "device/drivers/firmware-store-interface.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "utils/simple-timer.hpp"

/**
 * Moves a firmware image between devices over ESP-NOW broadcast. A seed
 * streams the image it is running, every chunk SEED_PASSES times, and listens
 * to nobody. A receiver asks the store about an image from its first chunk,
 * writes each chunk the first time it hears it, and once it has all of them
 * hands the image to the store, which decides whether to boot it.
 */
class FirmwareUpdateManager {
public:
    /**
     * isEligible is checked before an image is taken and on every sync() and
     * every chunk while one is being received; a receive is dropped once it
     * reads false. It must read false while this device seeds.
     */
    FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                          std::function<bool()> isEligible);
    /** Hands the radio handler back. */
    ~FirmwareUpdateManager();

    /** Starts streaming this device's running image; false if that image is unsigned or unreadable. */
    bool beginSeeding();
    /** True until the seed's last pass has been sent. */
    bool isSeeding() const;
    /** Call every loop pass, on every device: paces a seed run and drops a receive that stalls or stops being eligible. */
    void sync();

private:
    using ImageId = std::array<uint8_t, FIRMWARE_IMAGE_ID_LENGTH>;

    void onChunk(const FirmwareChunkHeader& header, const uint8_t* data, size_t length);
    bool startReceiving(const FirmwareChunkHeader& header, const uint8_t* data, size_t length);
    void writeChunk(uint16_t index, const uint8_t* data, size_t length);
    void commit();
    void stopReceiving();
    void sendChunk(uint16_t index);

    PeerCommsInterface* peerComms;
    FirmwareStoreInterface* firmwareStore;
    std::function<bool()> isEligible;

    // Seed.
    ImageId seedImageId{};
    uint32_t seedImageLength = 0;
    uint16_t seedChunkCount = 0;
    uint16_t nextChunk = 0;
    uint8_t passesLeft = 0;
    SimpleTimer beaconTimer;

    // Receiver.
    bool receiving = false;
    ImageId receiveImageId{};
    uint32_t receiveImageLength = 0;
    uint16_t receiveChunkCount = 0;
    uint16_t chunksWritten = 0;
    std::vector<bool> written;
    SimpleTimer receiveStallTimer;
    // The image the store last refused, ignored until refusedQuietTimer expires,
    // so a seed still streaming it is not downloaded again.
    ImageId refusedImageId{};
    SimpleTimer refusedQuietTimer;
};
