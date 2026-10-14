#pragma once

#include <cstddef>
#include <cstdint>

/// Both OTA slots in partitions.csv (ota_0/ota_1) are this size.
constexpr size_t OTA_PARTITION_SIZE = 0x3E0000;

/// Everything firmware distribution needs from flash, with no ESP32 types in
/// the signature, so the transport runs in the native tests against a fake.
class FirmwareStoreInterface {
public:
    /** Lets a derived store clean up through this base pointer. */
    virtual ~FirmwareStoreInterface() = default;

    /** Opens the inactive slot for an image of `length` bytes. */
    virtual bool beginWrite(size_t length) = 0;
    /** Writes into the slot opened by beginWrite, at any offset, in any order. */
    virtual bool writeAt(size_t offset, const uint8_t* data, size_t length) = 0;
    /**
     * Closes the write and accepts the image only if it checks out: intact,
     * signed by a key that also signed the running image, and newer than it.
     */
    virtual bool finishWrite() = 0;
    /** Discards the write opened by beginWrite; a no-op if none is open. */
    virtual void abortWrite() = 0;

    /** Capacity of the slot not currently running, in bytes. */
    virtual size_t getInactiveSlotSize() const = 0;
    /** Bytes of the running image as flashed, its signature block included. */
    virtual size_t getRunningImageLength() const = 0;
    /** Reads `length` bytes at `offset` from the running image. */
    virtual bool readRunningImage(size_t offset, uint8_t* out, size_t length) const = 0;
    /** The running image's identity: the SHA-256 of the ELF it was built from. */
    virtual void getRunningImageId(uint8_t out[32]) const = 0;

    /** Marks the written slot as the boot target, pending confirmation. */
    virtual bool setBootToWritten() = 0;
    /** Restarts the device; does not return on hardware. */
    virtual void restart() = 0;
};
