#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "device/drivers/firmware-store-interface.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "utils/simple-timer.hpp"

/** Seeds the running image by broadcast, and assembles a seed's image for the store to judge. */
class FirmwareUpdateManager {
public:
    /** Starts ineligible: nothing is received until setEligible(true). */
    FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore);
    /** Hands the radio handler back. */
    ~FirmwareUpdateManager();

    /** Whether a restart for an update is acceptable now; false drops a receive in progress. */
    void setEligible(bool eligible);
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

    PeerCommsInterface* peerComms;
    FirmwareStoreInterface* firmwareStore;
    bool eligible = false;

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
