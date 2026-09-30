#pragma once

#include <cstddef>
#include <cstdint>

/// Everything firmware distribution needs from flash, with no ESP32 types in
/// the signature. The native fake implements this so the protocol is testable
/// off-device; only the ESP32 implementation is not.
class FirmwareStoreInterface {
public:
    /// Lets a derived store clean up through this base pointer.
    virtual ~FirmwareStoreInterface() = default;

    /// Opens the inactive slot for `length` bytes, erasing only that much.
    virtual bool beginWrite(size_t length) = 0;
    /// Writes `length` bytes at `offset` into the slot opened by beginWrite.
    virtual bool writeAt(size_t offset, const uint8_t* data, size_t length) = 0;
    /// Finalizes the write opened by beginWrite.
    virtual bool finishWrite() = 0;
    /// Discards the write opened by beginWrite.
    virtual void abortWrite() = 0;

    /// Capacity of the slot not currently running, in bytes.
    virtual size_t getInactiveSlotSize() const = 0;
    /// Length in bytes of the currently running image's declared payload,
    /// excluding any trailer (cert/signature) appended after it.
    virtual size_t getRunningImageLength() const = 0;
    /// Reads `length` bytes at `offset` from the currently running image.
    virtual bool readRunningImage(size_t offset, uint8_t* out, size_t length) const = 0;
    /// Reads the trailer appended after the running image's declared length. Separate
    /// from readRunningImage because the trailer is credentials, not image bytes, and a
    /// caller must not have to know where one ends to find the other.
    virtual bool readRunningTrailer(uint8_t* out, size_t length) const = 0;
    /// Reads back what was written to the inactive slot, so a caller can verify the
    /// assembled image against its expected hash before booting it. Chunks arrive out
    /// of order, so the image cannot be hashed as it is written.
    virtual bool readWrittenSlot(size_t offset, uint8_t* out, size_t length) const = 0;

    /// Marks the written slot as the boot target, pending confirmation.
    virtual bool setBootToWritten() = 0;
    /// Confirms the running image so the bootloader stops treating it as pending.
    virtual void confirmRunningImage() = 0;

    /// Lowest firmware generation this device will accept.
    virtual uint8_t getMinGeneration() const = 0;
    /// Sets the lowest firmware generation this device will accept.
    virtual void setMinGeneration(uint8_t generation) = 0;
};
