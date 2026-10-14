#pragma once

#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_image_format.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "device/drivers/firmware-store-interface.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/logger.hpp"

// Rollback is the whole reason setBootToWritten() and the confirm in
// superviseRunningImage() are safe to call: without it esp_ota_set_boot_partition() marks the new slot
// ESP_OTA_IMG_UNDEFINED instead of NEW, the bootloader never promotes it to
// PENDING_VERIFY, the confirm below always early-returns, and an image that
// fails to boot is permanent. Nothing reads the flag at runtime, so the
// divergence is otherwise silent -- which is exactly how it was missed once.
#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
#error "firmware distribution requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"
#endif

/// FirmwareStoreInterface over the ESP32-S3's OTA partitions. IDF does the
/// checking: esp_ota_end validates the image hash and, with signed-app
/// verification on, its signature against the keys that signed the running
/// image; the app description carries the image id and build time.
class Esp32S3FirmwareStore : public FirmwareStoreInterface {
public:
    /**
     * Resolves the running and inactive OTA partitions. Construct it from the main task, as
     * early in setup() as possible: if the running image arrived
     * over the air and is still unconfirmed, this puts the calling task on the
     * task watchdog, so a hang anywhere before the confirm resets the device
     * and the bootloader rolls the image back.
     */
    Esp32S3FirmwareStore() {
        runningPartition = esp_ota_get_running_partition();
        esp_ota_img_states_t state;
        runningImagePending = runningPartition != nullptr &&
                              esp_ota_get_state_partition(runningPartition, &state) == ESP_OK &&
                              state == ESP_OTA_IMG_PENDING_VERIFY;
        updatePartition = esp_ota_get_next_update_partition(nullptr);
        if (updatePartition != nullptr && updatePartition->size != OTA_PARTITION_SIZE) {
            // Only the real partition table can show a partitions.csv resize.
            LOG_E(TAG, "OTA partition is %u bytes, code assumes %zu", updatePartition->size, OTA_PARTITION_SIZE);
        }
        guardPendingImage();
    }

    /// Aborts any write left open.
    ~Esp32S3FirmwareStore() override {
        if (writeOpen) {
            esp_ota_abort(otaHandle);
        }
    }

    /// Erases the inactive OTA partition for `length` bytes up front:
    /// esp_ota_write_with_offset does not erase, and chunks arrive out of order.
    bool beginWrite(size_t length) override {
        if (writeOpen) {
            esp_ota_abort(otaHandle);
            writeOpen = false;
        }
        if (updatePartition == nullptr) {
            LOG_E(TAG, "beginWrite: no inactive OTA partition available");
            return false;
        }
        const esp_err_t rc = esp_ota_begin(updatePartition, length, &otaHandle);
        if (rc != ESP_OK) {
            LOG_E(TAG, "esp_ota_begin failed: %d", rc);
            return false;
        }
        writeOpen = true;
        return true;
    }

    /// Writes via esp_ota_write_with_offset only: esp_ota_ops.h warns against
    /// mixing it with the sequential esp_ota_write.
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

    /// esp_ota_end checks the hash and the signature; the build times decide
    /// whether the image is newer than the one running.
    bool finishWrite() override {
        if (!writeOpen) {
            return false;
        }
        writeOpen = false;
        const esp_err_t rc = esp_ota_end(otaHandle);
        if (rc != ESP_OK) {
            LOG_E(TAG, "esp_ota_end rejected the image: %s", esp_err_to_name(rc));
            return false;
        }
        esp_app_desc_t written{};
        if (esp_ota_get_partition_description(updatePartition, &written) != ESP_OK) {
            LOG_E(TAG, "could not read the written image's description");
            return false;
        }
        const esp_app_desc_t* running = esp_app_get_description();
        if (buildTime(written) <= buildTime(*running)) {
            LOG_W(TAG, "image built %s %s is not newer than the running %s %s", written.date, written.time,
                  running->date, running->time);
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

    /// Size of the OTA partition not running.
    size_t getInactiveSlotSize() const override {
        return updatePartition != nullptr ? updatePartition->size : 0;
    }

    /// Cached once per boot: the image cannot change while it runs.
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

    /** The ELF SHA-256 IDF embeds in every app description. */
    void getRunningImageId(uint8_t out[32]) const override {
        std::memcpy(out, esp_app_get_description()->app_elf_sha256, 32);
    }

    /// Points the boot selector at the inactive partition (esp_ota_set_boot_partition).
    bool setBootToWritten() override {
        if (updatePartition == nullptr) {
            return false;
        }
        return esp_ota_set_boot_partition(updatePartition) == ESP_OK;
    }

    /**
     * Call every loop() pass: feeds the watchdog while the image is pending,
     * and confirms it once it has run PENDING_IMAGE_CONFIRM_MS since boot.
     */
    void superviseRunningImage() {
        if (!runningImagePending) {
            return;
        }
        if (static_cast<uint64_t>(esp_timer_get_time()) / 1000 < PENDING_IMAGE_CONFIRM_MS) {
            if (guardingPendingImage) {
                esp_task_wdt_reset();
            }
            return;
        }
        runningImagePending = false;
        esp_ota_mark_app_valid_cancel_rollback();
        if (guardingPendingImage) {
            guardingPendingImage = false;
            esp_task_wdt_delete(nullptr);
            reconfigureTaskWatchdog(CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000, BOOT_WATCHDOG_PANICS);
        }
    }

    /// Restarts the device (esp_restart()); does not return.
    void restart() override { esp_restart(); }

private:
    // The image as esptool built it, then the 4KB sector espsecure appends
    // for the signature block once the image is padded to a 4KB boundary.
    size_t computeRunningImageLength() const {
        if (runningPartition == nullptr) {
            return 0;
        }
        const esp_partition_pos_t position = {runningPartition->address, runningPartition->size};
        esp_image_metadata_t metadata{};
        if (esp_image_get_metadata(&position, &metadata) != ESP_OK) {
            LOG_E(TAG, "could not read the running image's metadata");
            return 0;
        }
        constexpr size_t SECTOR = 4096;
        return (metadata.image_len + SECTOR - 1) / SECTOR * SECTOR + SECTOR;
    }

    // Seconds since the epoch-free origin of __DATE__/__TIME__, enough to order
    // two builds: "Oct 14 2026" and "12:34:56" as IDF records them.
    static uint64_t buildTime(const esp_app_desc_t& desc) {
        static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char* month = std::strstr(MONTHS, std::string(desc.date, 3).c_str());
        const uint64_t monthIndex = month != nullptr ? static_cast<uint64_t>(month - MONTHS) / 3 : 0;
        const uint64_t day = std::strtoul(desc.date + 4, nullptr, 10);
        const uint64_t year = std::strtoul(desc.date + 7, nullptr, 10);
        const uint64_t hours = std::strtoul(desc.time, nullptr, 10);
        const uint64_t minutes = std::strtoul(desc.time + 3, nullptr, 10);
        const uint64_t seconds = std::strtoul(desc.time + 6, nullptr, 10);
        return ((((year * 12 + monthIndex) * 31 + day) * 24 + hours) * 60 + minutes) * 60 + seconds;
    }

    // Uptime before a pending image confirms itself: long enough to get through
    // setup, the radio bring-up and the first states.
    static constexpr uint64_t PENDING_IMAGE_CONFIRM_MS = 10000;

    // Covers setup() (a 2-3s splash and the radio bring-up), then the slowest
    // loop pass before the confirm: a WiFi connect polls for up to 5s, and an
    // HTTP exchange makes several blocking writes and reads, each capped at 10s.
    // A server slow enough that those waits add up past this resets a good
    // image and rolls it back. No update can be written meanwhile:
    // esp_ota_begin() refuses while the running image is pending.
    static constexpr uint32_t PENDING_IMAGE_WATCHDOG_MS = 30000;

    // Whether the boot-time watchdog configuration panics; restored on confirm.
#if CONFIG_ESP_TASK_WDT_PANIC
    static constexpr bool BOOT_WATCHDOG_PANICS = true;
#else
    static constexpr bool BOOT_WATCHDOG_PANICS = false;
#endif

    void guardPendingImage() {
        if (!runningImagePending) {
            return;
        }
        // Always panics: a watchdog that only logs would leave a hung image
        // running instead of rolling it back.
        reconfigureTaskWatchdog(PENDING_IMAGE_WATCHDOG_MS, true);
        guardingPendingImage = esp_task_wdt_add(nullptr) == ESP_OK;
        if (!guardingPendingImage) {
            LOG_E(TAG, "could not put the main task on the task watchdog");
            reconfigureTaskWatchdog(CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000, BOOT_WATCHDOG_PANICS);
        }
    }

    static void reconfigureTaskWatchdog(uint32_t timeoutMs, bool panic) {
        esp_task_wdt_config_t config = {};
        config.timeout_ms = timeoutMs;
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
        config.idle_core_mask |= 1 << 0;
#endif
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
        config.idle_core_mask |= 1 << 1;
#endif
        config.trigger_panic = panic;
        esp_task_wdt_reconfigure(&config);
    }

    static constexpr const char* TAG = "Esp32S3FirmwareStore";

    const esp_partition_t* runningPartition = nullptr;
    const esp_partition_t* updatePartition = nullptr;
    esp_ota_handle_t otaHandle = 0;
    bool writeOpen = false;
    // Read once at construction: whether the bootloader is waiting on this
    // image to confirm itself.
    bool runningImagePending = false;
    bool guardingPendingImage = false;

    mutable bool runningImageLengthComputed = false;
    mutable size_t cachedRunningImageLength = 0;
};
