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

/** Seeds the running image by broadcast, and assembles a seed's image for the store to judge. */
class FirmwareUpdateManager {
public:
    /** isEligible gates starting a receive and is rechecked on every sync() while writing; false drops it. */
    FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                          std::function<bool()> isEligible);
    /** Hands the radio handler back. */
    ~FirmwareUpdateManager();

    /** Starts streaming this device's running image; false if that image is unreadable. */
    bool beginSeeding();
    /** True from beginSeeding() until its last pass is queued. */
    bool isSeeding() const;
    /** Call every loop pass on every device. */
    void sync();

private:
    using ImageId = std::array<uint8_t, FIRMWARE_IMAGE_ID_LENGTH>;

    void onChunk(const FirmwareChunkHeader& header, const uint8_t* data, size_t length);
    bool startReceiving(const FirmwareChunkHeader& header, const uint8_t* data, size_t length);
    void writeChunk(uint16_t index, const uint8_t* data, size_t length);
    void commit();
    void stopReceiving();
    void sendChunk(uint16_t index);
    bool mayReceive() const;

    PeerCommsInterface* peerComms;
    FirmwareStoreInterface* firmwareStore;
    std::function<bool()> isEligible;

    ImageId seedImageId{};
    uint32_t seedImageLength = 0;
    uint16_t seedChunkCount = 0;
    uint16_t nextChunk = 0;
    uint8_t passesLeft = 0;
    SimpleTimer beaconTimer;

    // A refused image stays REFUSED until its chunks go quiet for RECEIVE_STALL_MS,
    // so a seed still streaming it is not downloaded again.
    enum class Receive : uint8_t {
        NONE,
        WRITING,
        REFUSED,
    };
    Receive receive = Receive::NONE;
    ImageId receiveImageId{};
    uint32_t receiveImageLength = 0;
    uint16_t receiveChunkCount = 0;
    uint16_t chunksWritten = 0;
    std::vector<bool> written;
    std::vector<uint8_t> chunkZero;
    SimpleTimer quietTimer;
};
