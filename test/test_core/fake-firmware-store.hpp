#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "device/drivers/firmware-store-interface.hpp"

/**
 * In-memory FirmwareStoreInterface. finishWrite stands in for IDF's checks
 * (hash, signature, newer build) with a single acceptImages switch.
 */
class FakeFirmwareStore : public FirmwareStoreInterface {
public:
    /** Opens a zero-filled slot of `length` bytes. */
    bool beginWrite(size_t length) override {
        beginWriteCalls++;
        slot.assign(length, 0);
        writeOpen = true;
        return true;
    }
    /** Copies into the open slot; fails past its end or with none open. */
    bool writeAt(size_t offset, const uint8_t* data, size_t length) override {
        if (!writeOpen || offset + length > slot.size()) {
            return false;
        }
        std::copy(data, data + length, slot.begin() + static_cast<std::ptrdiff_t>(offset));
        writeCount++;
        return true;
    }
    /** Closes the slot and returns acceptImages. */
    bool finishWrite() override {
        writeOpen = false;
        finished = true;
        return acceptImages;
    }
    /** Closes the slot without accepting it. */
    void abortWrite() override {
        if (writeOpen) {
            abortCalls++;
        }
        writeOpen = false;
    }
    /** Defaults to a full OTA partition. */
    size_t getInactiveSlotSize() const override { return inactiveSlotSize; }
    /** Length of the bytes given to setRunningImage. */
    size_t getRunningImageLength() const override { return runningImage.size(); }
    /** Serves the bytes given to setRunningImage. */
    bool readRunningImage(size_t offset, uint8_t* out, size_t length) const override {
        if (offset + length > runningImage.size()) {
            return false;
        }
        std::copy(runningImage.begin() + static_cast<std::ptrdiff_t>(offset),
                  runningImage.begin() + static_cast<std::ptrdiff_t>(offset + length), out);
        return true;
    }
    /** The id given to setRunningImage. */
    void getRunningImageId(uint8_t out[32]) const override { std::memcpy(out, runningId, 32); }
    /** Records the call. */
    bool setBootToWritten() override {
        bootSet = true;
        return true;
    }
    /** Records the call instead of restarting. */
    void restart() override { restarted = true; }

    /** Sets the running image's bytes and its id (every byte `idByte`). */
    void setRunningImage(const std::vector<uint8_t>& bytes, uint8_t idByte) {
        runningImage = bytes;
        std::memset(runningId, idByte, sizeof(runningId));
    }

    std::vector<uint8_t> slot;
    bool acceptImages = true;
    size_t inactiveSlotSize = OTA_PARTITION_SIZE;
    int beginWriteCalls = 0;
    int writeCount = 0;
    int abortCalls = 0;
    bool finished = false;
    bool bootSet = false;
    bool restarted = false;

private:
    std::vector<uint8_t> runningImage;
    uint8_t runningId[32] = {};
    bool writeOpen = false;
};
