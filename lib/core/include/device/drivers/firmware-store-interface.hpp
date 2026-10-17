#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Everything firmware distribution needs from flash, with no ESP32 types in
 * the signature, so the transport runs in the native tests against a fake.
 */
class FirmwareStoreInterface {
public:
    /** Lets a derived store clean up through this base pointer. */
    virtual ~FirmwareStoreInterface() = default;

    /**
     * Opens the inactive slot for an image of `length` bytes that starts with
     * `head`, but first judges the app description in `head`: the image must
     * describe itself as built for this device's family and newer than the
     * running image (unverified; finishWrite has the final say). Nothing is
     * erased for an image refused here. Also false if the slot cannot be opened:
     * a length of 0 or larger than the slot, an unsigned running image, or a
     * running image not yet confirmed.
     */
    virtual bool beginWrite(const uint8_t* head, size_t headLength, size_t length) = 0;
    /** Writes into the slot opened by beginWrite, at any offset, in any order. */
    virtual bool writeAt(size_t offset, const uint8_t* data, size_t length) = 0;
    /**
     * Closes the write and makes the image the next to boot, but only if it is
     * intact, signed with the key that signed the running image, built for this
     * family, and newer. A refused image has its first sector erased.
     */
    virtual bool finishWrite() = 0;
    /** Ends the write opened by beginWrite without making its image bootable; a no-op if none is open. */
    virtual void abortWrite() = 0;

    /** Bytes of the running image as flashed, signature sector included; 0 if it is unsigned or unreadable. */
    virtual size_t getRunningImageLength() const = 0;
    /** Reads `length` bytes at `offset` from the running image. */
    virtual bool readRunningImage(size_t offset, uint8_t* out, size_t length) const = 0;
    /** The running image's identity: the SHA-256 of the ELF it was built from. */
    virtual void getRunningImageId(uint8_t out[32]) const = 0;

    /** Restarts the device; does not return on hardware. */
    virtual void restart() = 0;
};
