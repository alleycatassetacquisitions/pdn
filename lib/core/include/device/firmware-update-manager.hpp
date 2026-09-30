#pragma once

#include <cstddef>
#include <cstdint>

#include "device/drivers/firmware-store-interface.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/peer-comms-types.hpp"

/// Collects a firmware image into the inactive flash slot as OFFER and CHUNK
/// frames arrive. Chunks land in whatever order the radio delivers them; a
/// bitmap tracks what has landed so a later repair round can ask for gaps.
/// Depends only on PeerCommsInterface and FirmwareStoreInterface, so it
/// builds and runs in the native test suite with no ESP32 headers in reach.
class FirmwareUpdateManager {
public:
    /// Wires the manager to the radio and flash slot and registers to
    /// receive kFirmwareUpdate frames. rootPublicKey pins the chain every
    /// offer must verify against.
    FirmwareUpdateManager(PeerCommsInterface* peerComms, FirmwareStoreInterface* firmwareStore,
                          const uint8_t* rootPublicKey);

    /// Evaluates an announced image. Ignores it outright if it matches the
    /// running image or the image already being collected; otherwise, once
    /// it verifies and fits the inactive slot, opens the slot for the
    /// declared length and starts collecting chunks.
    void onOffer(const uint8_t* fromMac, const FirmwareOfferPayload& offer);

    /// Writes one chunk into the open slot at its declared index. Drops it
    /// if no offer is open, the index is out of range for it, or that index
    /// already landed.
    void onChunk(const FirmwareChunkHeader& header, const uint8_t* data);

    /// True once an offer has been accepted; nothing here clears it back to
    /// false.
    bool isReceiving() const;

    /// Count of distinct chunk indices written so far.
    uint16_t receivedCount() const;

    /// Copies the receive bitmap (bit N set means chunk N landed) into `out`.
    void fillBitmap(uint8_t out[FIRMWARE_BITMAP_BYTES]) const;

private:
    // Routes a raw radio frame to onOffer/onChunk, validating its length
    // against the wire struct before any cast — verifyOffer takes a const&
    // and cannot check the frame it was handed.
    static void dispatchPacket(const uint8_t* src, const uint8_t* data, size_t length, void* ctx);
    void onPacketReceived(const uint8_t* src, const uint8_t* data, size_t length);

    bool bitmapBit(uint16_t index) const;
    void setBitmapBit(uint16_t index);

    // The running image's bytes cannot change while it is executing, so its
    // hash is computed at most once per boot and reused from then on.
    const uint8_t* cachedRunningImageHash();

    PeerCommsInterface* peerComms;
    FirmwareStoreInterface* firmwareStore;
    const uint8_t* rootPublicKey;

    bool receiving = false;
    uint16_t chunkCount = 0;
    uint16_t chunkSize = 0;
    uint16_t receivedChunkCount = 0;
    size_t imageLength = 0;
    uint8_t currentImageHash[FIRMWARE_SHA256_LENGTH] = {};
    uint8_t bitmap[FIRMWARE_BITMAP_BYTES] = {};

    bool runningImageHashComputed = false;
    uint8_t runningImageHash[FIRMWARE_SHA256_LENGTH] = {};
};
