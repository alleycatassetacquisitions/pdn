#pragma once

#include <cstddef>
#include <cstdint>

/** Flash access for firmware distribution, free of ESP32 types so native tests can fake it. */
class FirmwareStoreInterface {
public:
    /** Lets a derived store clean up through this base pointer. */
    virtual ~FirmwareStoreInterface() = default;

    /**
     * Opens the inactive slot for `length` bytes if `head`'s app description is newer and the
     * running image is confirmed; erases nothing when it refuses.
     */
    virtual bool beginWrite(const uint8_t* head, size_t headLength, size_t length) = 0;
    /** Writes into the slot opened by beginWrite, at any offset, in any order. */
    virtual bool writeAt(size_t offset, const uint8_t* data, size_t length) = 0;
    /**
     * Makes the image next to boot if intact, signed with the running image's key, and newer;
     * otherwise leaves it unbootable.
     */
    virtual bool finishWrite() = 0;
    /** Ends the write opened by beginWrite without making its image bootable; a no-op if none is open. */
    virtual void abortWrite() = 0;

    /** Bytes of the running image as flashed, signature sector included; 0 if it is unreadable. */
    virtual size_t getRunningImageLength() const = 0;
    /** Reads `length` bytes at `offset` from the running image. */
    virtual bool readRunningImage(size_t offset, uint8_t* out, size_t length) const = 0;
    /** The running image's identity: the SHA-256 of the ELF it was built from. */
    virtual void getRunningImageId(uint8_t out[32]) const = 0;

    /** Restarts the device; does not return on hardware. */
    virtual void restart() = 0;
};
