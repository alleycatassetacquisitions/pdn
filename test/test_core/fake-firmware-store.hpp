#pragma once

#include "device/drivers/firmware-store-interface.hpp"
#include <algorithm>
#include <vector>

/// In-memory FirmwareStoreInterface for native tests: backs the inactive
/// slot with a vector instead of flash so Tasks 8-12 can drive firmware
/// distribution without an ESP32.
class FakeFirmwareStore : public FirmwareStoreInterface {
public:
    /// Fails if `length` exceeds the configured inactive slot size.
    bool beginWrite(size_t length) override {
        beginWriteCallCount++;
        pendingBeginWriteLength = length;
        if (length > inactiveSlotSize) {
            return false;
        }
        writeSlot.assign(length, 0);
        writeInProgress = true;
        return true;
    }

    /// Fails past the slot bounds (recorded via wroteOutsideImage) or past a
    /// configured failWritesFrom offset, to let tests simulate a flash fault.
    bool writeAt(size_t offset, const uint8_t* data, size_t length) override {
        if (!writeInProgress) {
            return false;
        }
        if (offset > writeSlot.size() || length > writeSlot.size() - offset) {
            writeOutsideImageOccurred = true;
            return false;
        }
        if (failWritesEnabled && offset >= failFromOffset) {
            return false;
        }
        std::copy(data, data + length, writeSlot.begin() + static_cast<std::ptrdiff_t>(offset));
        return true;
    }

    /// Closes the write opened by beginWrite; fails if none is open.
    bool finishWrite() override {
        if (!writeInProgress) {
            return false;
        }
        writeInProgress = false;
        return true;
    }

    /// Discards the in-progress write and clears the slot contents.
    void abortWrite() override {
        writeInProgress = false;
        writeSlot.clear();
    }

    /// Capacity configured via setInactiveSlotSize.
    size_t getInactiveSlotSize() const override { return inactiveSlotSize; }
    /// Length of the image configured via setRunningImage.
    size_t getRunningImageLength() const override { return runningImage.size(); }

    /// Fails if the requested range falls outside the running image plus
    /// its trailer. A real running partition is one contiguous flash
    /// region, so a read starting past getRunningImageLength() reaches the
    /// trailer the same way it reaches the image: through this call, not a
    /// separate accessor.
    bool readRunningImage(size_t offset, uint8_t* out, size_t length) const override {
        const size_t total = runningImage.size() + runningTrailer.size();
        if (offset > total || length > total - offset) {
            return false;
        }
        size_t pos = offset;
        size_t remaining = length;
        if (pos < runningImage.size()) {
            const size_t fromImage = std::min(remaining, runningImage.size() - pos);
            std::copy(runningImage.begin() + static_cast<std::ptrdiff_t>(pos),
                      runningImage.begin() + static_cast<std::ptrdiff_t>(pos + fromImage), out);
            out += fromImage;
            pos += fromImage;
            remaining -= fromImage;
        }
        if (remaining > 0) {
            const size_t trailerOffset = pos - runningImage.size();
            std::copy(runningTrailer.begin() + static_cast<std::ptrdiff_t>(trailerOffset),
                      runningTrailer.begin() + static_cast<std::ptrdiff_t>(trailerOffset + remaining), out);
        }
        return true;
    }

    /// Reads back from the same backing store writeAt fills, regardless of
    /// chunk order, so a caller can hash the assembled image once complete.
    bool readWrittenSlot(size_t offset, uint8_t* out, size_t length) const override {
        if (offset > writeSlot.size() || length > writeSlot.size() - offset) {
            return false;
        }
        std::copy(writeSlot.begin() + static_cast<std::ptrdiff_t>(offset),
                  writeSlot.begin() + static_cast<std::ptrdiff_t>(offset + length), out);
        return true;
    }

    /// Records the call for bootSet() assertions.
    bool setBootToWritten() override {
        bootToWrittenSet = true;
        return true;
    }

    /// No slot promotion to simulate: the fake models one boot session.
    void confirmRunningImage() override {}

    /// Generation floor configured via setMinGeneration.
    uint8_t getMinGeneration() const override { return minGeneration; }
    /// Stores the generation floor for later getMinGeneration calls.
    void setMinGeneration(uint8_t generation) override { minGeneration = generation; }

    /// Bytes written so far into the open (or last finished) slot.
    const std::vector<uint8_t>& slot() const { return writeSlot; }
    /// Length passed to the most recent beginWrite call.
    size_t beginWriteLength() const { return pendingBeginWriteLength; }
    /// Number of times beginWrite has been called.
    int beginWriteCalls() const { return beginWriteCallCount; }
    /// Whether setBootToWritten has been called.
    bool bootSet() const { return bootToWrittenSet; }
    /// Whether a writeAt call ever landed outside the declared slot bounds.
    bool wroteOutsideImage() const { return writeOutsideImageOccurred; }

    /// Configures the capacity beginWrite checks against.
    void setInactiveSlotSize(size_t bytes) { inactiveSlotSize = bytes; }
    /// Seeds the bytes readRunningImage and getRunningImageLength serve.
    void setRunningImage(const std::vector<uint8_t>& bytes) { runningImage = bytes; }
    /// Seeds the bytes readRunningImage serves past getRunningImageLength():
    /// the signer cert and image signature a seed reads before offering.
    void setRunningTrailer(const std::vector<uint8_t>& bytes) { runningTrailer = bytes; }
    /// Makes writeAt fail for any offset at or past `offset`, simulating a
    /// flash write fault partway through an image.
    void failWritesFrom(size_t offset) {
        failWritesEnabled = true;
        failFromOffset = offset;
    }

private:
    std::vector<uint8_t> writeSlot;
    std::vector<uint8_t> runningImage;
    std::vector<uint8_t> runningTrailer;
    size_t inactiveSlotSize = 0;
    size_t pendingBeginWriteLength = 0;
    int beginWriteCallCount = 0;
    bool writeInProgress = false;
    bool bootToWrittenSet = false;
    bool writeOutsideImageOccurred = false;
    bool failWritesEnabled = false;
    size_t failFromOffset = 0;
    uint8_t minGeneration = 0;
};
