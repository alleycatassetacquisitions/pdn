#pragma once

#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <Preferences.h>

#include "device/drivers/firmware-store-interface.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/logger.hpp"

/// FirmwareStoreInterface over the ESP32-S3's OTA partitions (esp_ota_*
/// against ota_0/ota_1) and NVS (the generation floor). The only
/// implementation of this interface with real ESP32 types in it; everything
/// else in firmware distribution depends only on the interface, per that
/// header's own contract, so it builds and runs in the native test suite too.
class Esp32S3FirmwareStore : public FirmwareStoreInterface {
public:
    /// Resolves the running and inactive OTA partitions and opens this
    /// device's generation-floor NVS namespace.
    Esp32S3FirmwareStore() {
        runningPartition = esp_ota_get_running_partition();
        updatePartition = esp_ota_get_next_update_partition(nullptr);
        if (updatePartition != nullptr && updatePartition->size != OTA_PARTITION_SIZE) {
            // The bitmap-ceiling static_assert in firmware-update-manager.cpp
            // only catches a SEED_CHUNK_SIZE change; it cannot see a
            // partitions.csv resize. This constructor is the one place that
            // can read the real partition table, so it is the one place that
            // can catch that drift.
            LOG_E(TAG, "OTA partition is %u bytes, code assumes %zu", updatePartition->size, OTA_PARTITION_SIZE);
        }
        prefs.begin(FIRMWARE_STORE_NVS_NAMESPACE, /*readOnly=*/false);
    }

    /// Aborts any write left open and closes the NVS namespace.
    ~Esp32S3FirmwareStore() override {
        if (writeOpen) {
            esp_ota_abort(otaHandle);
        }
        prefs.end();
    }

    /// Erases the inactive OTA partition for `length` bytes plus headroom for
    /// the trailer the commit path writes past it: esp_ota_write_with_offset
    /// does not erase, and chunks arrive out of order, so everything a write
    /// will ever touch has to be erased up front. Never
    /// OTA_WITH_SEQUENTIAL_WRITES, which assumes sequential writes.
    bool beginWrite(size_t length) override {
        if (writeOpen) {
            esp_ota_abort(otaHandle);
            writeOpen = false;
        }
        if (updatePartition == nullptr) {
            LOG_E(TAG, "beginWrite: no inactive OTA partition available");
            return false;
        }
        const esp_err_t rc = esp_ota_begin(updatePartition, length + sizeof(FirmwareTrailer), &otaHandle);
        if (rc != ESP_OK) {
            LOG_E(TAG, "esp_ota_begin failed: %d", rc);
            return false;
        }
        writeOpen = true;
        return true;
    }

    /// Writes via esp_ota_write_with_offset only — esp_ota_ops.h warns
    /// against mixing that with the sequential esp_ota_write, and this store
    /// never calls the latter. Used for both chunk data and, once collection
    /// finishes, the trailer.
    bool writeAt(size_t offset, const uint8_t* data, size_t length) override {
        if (!writeOpen) {
            return false;
        }
        const esp_err_t rc = esp_ota_write_with_offset(otaHandle, data, length, static_cast<uint32_t>(offset));
        if (rc != ESP_OK) {
            LOG_E(TAG, "esp_ota_write_with_offset failed at offset %zu: %d", offset, rc);
            return false;
        }
        return true;
    }

    /// Closes and validates the write opened by beginWrite (esp_ota_end).
    bool finishWrite() override {
        if (!writeOpen) {
            return false;
        }
        writeOpen = false;
        const esp_err_t rc = esp_ota_end(otaHandle);
        if (rc != ESP_OK) {
            LOG_E(TAG, "esp_ota_end failed: %d", rc);
            return false;
        }
        return true;
    }

    /// Discards the write opened by beginWrite (esp_ota_abort); a no-op if
    /// none is open, including after finishWrite has already closed it.
    void abortWrite() override {
        if (!writeOpen) {
            return;
        }
        writeOpen = false;
        esp_ota_abort(otaHandle);
    }

    /// Real size of the OTA partition esp_ota_get_next_update_partition
    /// returned at construction, less the trailer's own room: beginWrite
    /// erases `length + sizeof(FirmwareTrailer)`, so an offer this check
    /// accepted at the raw partition size would still fail there.
    size_t getInactiveSlotSize() const override {
        return updatePartition != nullptr ? updatePartition->size - sizeof(FirmwareTrailer) : 0;
    }

    /// Derived once per boot by walking the running image's own header (see
    /// computeRunningImageLength) and cached from then on: the image cannot
    /// change while it is executing.
    size_t getRunningImageLength() const override {
        if (!runningImageLengthComputed) {
            cachedRunningImageLength = computeRunningImageLength();
            runningImageLengthComputed = true;
        }
        return cachedRunningImageLength;
    }

    /// Reads directly from the running OTA partition.
    bool readRunningImage(size_t offset, uint8_t* out, size_t length) const override {
        if (runningPartition == nullptr) {
            return false;
        }
        return esp_partition_read(runningPartition, offset, out, length) == ESP_OK;
    }

    /// Reads from the running partition at getRunningImageLength() — where a
    /// signed build's post-build step appended the trailer.
    bool readRunningTrailer(uint8_t* out, size_t length) const override {
        if (runningPartition == nullptr) {
            return false;
        }
        return esp_partition_read(runningPartition, getRunningImageLength(), out, length) == ESP_OK;
    }

    /// Reads back from the inactive OTA partition, regardless of whether its
    /// write is still open — esp_ota_write_with_offset writes straight to
    /// flash rather than buffering, so this is consistent as soon as
    /// writeAt returns.
    bool readWrittenSlot(size_t offset, uint8_t* out, size_t length) const override {
        if (updatePartition == nullptr) {
            return false;
        }
        return esp_partition_read(updatePartition, offset, out, length) == ESP_OK;
    }

    /// Points the boot selector at the inactive partition (esp_ota_set_boot_partition).
    bool setBootToWritten() override {
        if (updatePartition == nullptr) {
            return false;
        }
        return esp_ota_set_boot_partition(updatePartition) == ESP_OK;
    }

    /// Confirms the running image only if the bootloader is actually still
    /// waiting on one (ESP_OTA_IMG_PENDING_VERIFY) — a USB-flashed image
    /// never is, and reaching into OTA machinery on every boot of every
    /// device for no reason is the failure mode this guard avoids.
    void confirmRunningImage() override {
        if (runningPartition == nullptr) {
            return;
        }
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(runningPartition, &state) != ESP_OK) {
            return;
        }
        if (state != ESP_OTA_IMG_PENDING_VERIFY) {
            return;
        }
        esp_ota_mark_app_valid_cancel_rollback();
    }

    /// Restarts the device (esp_restart()); does not return.
    void restart() override { esp_restart(); }

    /// Reads the generation floor from NVS; 0 (accept everything) if never set.
    uint8_t getMinGeneration() const override { return prefs.getUChar(MIN_GENERATION_KEY, 0); }

    /// Persists the generation floor to NVS so it survives a reboot —
    /// revocation only works if the floor a device refuses below is durable.
    void setMinGeneration(uint8_t generation) override { prefs.putUChar(MIN_GENERATION_KEY, generation); }

private:
    // Walks the running image's own header exactly as the ROM bootloader
    // does: nothing else records this device's own image length. A
    // USB-flashed device has no OFFER and no NVS entry for it, and that is
    // the only case that matters for a device's first-ever seed.
    size_t computeRunningImageLength() const {
        if (runningPartition == nullptr) {
            return 0;
        }
        esp_image_header_t header{};
        if (esp_partition_read(runningPartition, 0, &header, sizeof(header)) != ESP_OK ||
            header.magic != ESP_IMAGE_HEADER_MAGIC) {
            LOG_E(TAG, "computeRunningImageLength: could not read a valid image header");
            return 0;
        }

        size_t offset = sizeof(esp_image_header_t);
        for (uint8_t i = 0; i < header.segment_count; i++) {
            esp_image_segment_header_t segment{};
            if (esp_partition_read(runningPartition, offset, &segment, sizeof(segment)) != ESP_OK) {
                LOG_E(TAG, "computeRunningImageLength: could not read segment header %u", i);
                return 0;
            }
            offset += sizeof(esp_image_segment_header_t) + segment.data_len;
        }

        offset += 1;                           // the 1-byte checksum
        offset = (offset + 15) & ~size_t{15};  // padded out to a 16-byte boundary
        if (header.hash_appended) {
            offset += 32;  // esptool's appended SHA-256 "simple hash"
        }
        return offset;
    }

    static constexpr const char* TAG = "Esp32S3FirmwareStore";
    static constexpr const char* FIRMWARE_STORE_NVS_NAMESPACE = "fw-store";
    static constexpr const char* MIN_GENERATION_KEY = "minGen";

    const esp_partition_t* runningPartition = nullptr;
    const esp_partition_t* updatePartition = nullptr;
    esp_ota_handle_t otaHandle = 0;
    bool writeOpen = false;

    mutable bool runningImageLengthComputed = false;
    mutable size_t cachedRunningImageLength = 0;

    mutable Preferences prefs;
};
