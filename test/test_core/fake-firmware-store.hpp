#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "device/drivers/firmware-store-interface.hpp"

/**
 * In-memory FirmwareStoreInterface. The judgements the ESP32 store reads from
 * app descriptions and IDF's checks are two switches: wantImages for the one
 * beginWrite makes before opening the slot, acceptImages for finishWrite.
 */
class FakeFirmwareStore : public FirmwareStoreInterface {
public:
    /** Records what it was asked to judge, then opens a zero-filled slot of `length` bytes if wantImages. */
    bool beginWrite(const uint8_t* head, size_t headLength, size_t length) override {
        lastHead.assign(head, head + headLength);
        lastLength = length;
        if (!wantImages || length == 0) {
            return false;
        }
        beginWriteCalls++;
        slot.assign(length, 0);
        writeOpen = true;
        return true;
    }
    /** Counts the call, then copies into the open slot; fails past its end or with none open. */
    bool writeAt(size_t offset, const uint8_t* data, size_t length) override {
        writeCount++;
        if (!writeOpen || offset + length > slot.size()) {
            return false;
        }
        std::copy(data, data + length, slot.begin() + static_cast<std::ptrdiff_t>(offset));
        return true;
    }
    /** Closes the slot; acceptImages decides whether it boots. */
    bool finishWrite() override {
        writeOpen = false;
        bootSet = acceptImages;
        return acceptImages;
    }
    /** Closes the slot without keeping it, counting the abort. */
    void abortWrite() override {
        if (writeOpen) {
            abortCalls++;
        }
        writeOpen = false;
    }
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
    /** Records the call instead of restarting. */
    void restart() override { restarted = true; }

    /** Sets the running image's bytes and its id (every byte `idByte`). */
    void setRunningImage(const std::vector<uint8_t>& bytes, uint8_t idByte) {
        runningImage = bytes;
        std::memset(runningId, idByte, sizeof(runningId));
    }

    std::vector<uint8_t> slot;
    bool wantImages = true;
    bool acceptImages = true;
    std::vector<uint8_t> lastHead;
    size_t lastLength = 0;
    int beginWriteCalls = 0;
    int writeCount = 0;
    int abortCalls = 0;
    bool bootSet = false;
    bool restarted = false;

private:
    std::vector<uint8_t> runningImage;
    uint8_t runningId[32] = {};
    bool writeOpen = false;
};
