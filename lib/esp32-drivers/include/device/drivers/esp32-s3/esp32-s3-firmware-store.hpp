#pragma once

#include <esp_app_format.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <spi_flash_mmap.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <tuple>

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include "utils/simple-timer.hpp"

// Without rollback a new image never reaches PENDING_VERIFY, so the confirm
// below never runs and an image that boots and then crashes or hangs stays
// installed.
#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
#error "firmware distribution requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"
#endif

// Without signature checks on update, esp_ota_end verifies only the image's
// hash, and any intact image with a later build time installs from any radio in
// range.
#if !defined(CONFIG_SECURE_SIGNED_ON_UPDATE)
#error "firmware distribution requires CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT and CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT"
#endif

// The build time in each image's app description is what orders images; without
// it every image reads as not newer and no device takes an update.
#if !defined(CONFIG_APP_COMPILE_TIME_DATE)
#error "firmware distribution requires CONFIG_APP_COMPILE_TIME_DATE"
#endif

// A watchdog that only logs would leave a hung image running instead of rolling
// it back.
#if !defined(CONFIG_ESP_TASK_WDT_PANIC)
#error "firmware distribution requires CONFIG_ESP_TASK_WDT_PANIC"
#endif

// The watchdog must outlast the slowest loop pass before the confirm (see
// sdkconfig.defaults.pdn), or it resets a good image and rolls it back.
#if CONFIG_ESP_TASK_WDT_TIMEOUT_S < 30
#error "firmware distribution requires CONFIG_ESP_TASK_WDT_TIMEOUT_S of at least 30"
#endif

/**
 * FirmwareStoreInterface over the ESP32-S3's OTA partitions. IDF checks the
 * signature in esp_ota_end, against the key that signed the running image;
 * this store adds the rest of the rule from each image's app description: a
 * later build time.
 */
class Esp32S3FirmwareStore : public FirmwareStoreDriverInterface {
public:
    /**
     * Resolves the running and inactive OTA partitions. Construct it from the
     * main task, as early in setup() as possible: if the running image arrived
     * over the air and is still unconfirmed, this puts the calling task on the
     * task watchdog, so a hang in that task between here and the confirm resets
     * the device and the bootloader rolls the image back.
     */
    explicit Esp32S3FirmwareStore(const std::string& name)
        : FirmwareStoreDriverInterface(name) {
        runningPartition = esp_ota_get_running_partition();
        updatePartition = esp_ota_get_next_update_partition(nullptr);
        esp_ota_img_states_t state;
        runningImagePending = runningPartition != nullptr &&
                              esp_ota_get_state_partition(runningPartition, &state) == ESP_OK &&
                              state == ESP_OTA_IMG_PENDING_VERIFY;
        guardingPendingImage = runningImagePending && esp_task_wdt_add(nullptr) == ESP_OK;
        if (runningImagePending && !guardingPendingImage) {
            LOG_E(TAG, "could not put the main task on the task watchdog");
        }
    }

    /**
     * Judges the app description in `head`, then erases the inactive partition
     * for `length` bytes up front: esp_ota_write_with_offset does not erase, and
     * chunks arrive out of order.
     */
    bool beginWrite(const uint8_t* head, size_t headLength, size_t length) override {
        // esp_ota_begin reads a length of 0 as "erase the whole partition".
        if (updatePartition == nullptr || length == 0 || length > updatePartition->size ||
            headLength < APP_DESC_OFFSET + sizeof(esp_app_desc_t)) {
            return false;
        }
        esp_app_desc_t candidate{};
        std::memcpy(&candidate, head + APP_DESC_OFFSET, sizeof(candidate));
        if (!isNewer(candidate, false)) {
            return false;
        }
        const esp_err_t rc = esp_ota_begin(updatePartition, length, &otaHandle);
        if (rc != ESP_OK) {
            LOG_E(TAG, "esp_ota_begin failed: %s", esp_err_to_name(rc));
            otaHandle = 0;
            return false;
        }
        return true;
    }

    /** esp_ota_write_with_offset only: esp_ota_ops.h warns against mixing it with esp_ota_write. */
    bool writeAt(size_t offset, const uint8_t* data, size_t length) override {
        return esp_ota_write_with_offset(otaHandle, data, length, static_cast<uint32_t>(offset)) == ESP_OK;
    }

    /** esp_ota_end checks the signature; the app description decides the rest. */
    bool finishWrite() override {
        const esp_err_t rc = esp_ota_end(otaHandle);
        otaHandle = 0;
        esp_app_desc_t written{};
        bool accepted = rc == ESP_OK;
        if (!accepted) {
            LOG_E(TAG, "esp_ota_end refused the image: %s", esp_err_to_name(rc));
        } else {
            accepted = esp_ota_get_partition_description(updatePartition, &written) == ESP_OK &&
                       isNewer(written, true) && esp_ota_set_boot_partition(updatePartition) == ESP_OK;
        }
        if (!accepted) {
            makeUnbootable();
        }
        return accepted;
    }

    /** Releases the write and erases the first sector of what it wrote. */
    void abortWrite() override {
        if (otaHandle != 0) {
            esp_ota_abort(otaHandle);
            otaHandle = 0;
            makeUnbootable();
        }
    }

    /** The image as esptool built it, then the 4KB sector espsecure appends for the signature. */
    size_t getRunningImageLength() const override {
        const esp_partition_pos_t position = {runningPartition->address, runningPartition->size};
        esp_image_metadata_t metadata{};
        if (esp_image_get_metadata(&position, &metadata) != ESP_OK) {
            LOG_E(TAG, "could not read the running image's metadata");
            return 0;
        }
        return (metadata.image_len + SPI_FLASH_SEC_SIZE - 1) / SPI_FLASH_SEC_SIZE * SPI_FLASH_SEC_SIZE +
               SPI_FLASH_SEC_SIZE;
    }

    /** Reads directly from the running OTA partition. */
    bool readRunningImage(size_t offset, uint8_t* out, size_t length) const override {
        return runningPartition != nullptr && esp_partition_read(runningPartition, offset, out, length) == ESP_OK;
    }

    /** The ELF SHA-256 IDF embeds in every app description. */
    void getRunningImageId(uint8_t out[32]) const override {
        std::memcpy(out, esp_app_get_description()->app_elf_sha256, 32);
    }

    /** Nothing to set up beyond the constructor. */
    int initialize() override { return 0; }

    /**
     * Runs every loop() pass: feeds the watchdog while the image is pending,
     * and confirms it once loop() has run for PENDING_IMAGE_CONFIRM_MS.
     */
    void exec() override {
        if (!runningImagePending) {
            return;
        }
        if (!confirmTimer.isRunning()) {
            confirmTimer.setTimer(PENDING_IMAGE_CONFIRM_MS);
        }
        if (!confirmTimer.expired()) {
            if (guardingPendingImage) {
                esp_task_wdt_reset();
            }
            return;
        }
        runningImagePending = false;
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
            LOG_E(TAG, "could not confirm the running image; the next boot rolls it back");
        }
        if (guardingPendingImage) {
            guardingPendingImage = false;
            esp_task_wdt_delete(nullptr);
        }
    }

    /** Restarts the device (esp_restart()); does not return. */
    void restart() override { esp_restart(); }

private:
    // An image left intact in the inactive slot, refused or abandoned, stays
    // bootable: the bootloader checks no signature at boot, and falls back to any
    // loadable slot if the selected one fails. A forger can leave one there by
    // declaring an image longer than the intact one it sends and letting the
    // receive stall.
    void makeUnbootable() { esp_partition_erase_range(updatePartition, 0, SPI_FLASH_SEC_SIZE); }

    // Where the app description sits in an image, as esp_ota_get_partition_description reads it.
    static constexpr size_t APP_DESC_OFFSET = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);

    // Built later than the running image.
    static bool isNewer(const esp_app_desc_t& candidate, bool explain) {
        const esp_app_desc_t* running = esp_app_get_description();
        if (candidate.magic_word != ESP_APP_DESC_MAGIC_WORD) {
            return false;
        }
        if (!builtAfter(candidate, *running)) {
            if (explain) {
                LOG_W(TAG, "image built %.16s %.16s is not newer than the running %.16s %.16s", candidate.date,
                      candidate.time, running->date, running->time);
            }
            return false;
        }
        return true;
    }

    // Builds are stamped in UTC (firmware_commit_hash.py sets SOURCE_DATE_EPOCH);
    // an unparseable stamp counts as not newer.
    static bool builtAfter(const esp_app_desc_t& later, const esp_app_desc_t& earlier) {
        std::tm a{};
        std::tm b{};
        if (!parseBuildTime(later, a) || !parseBuildTime(earlier, b)) {
            return false;
        }
        return std::tie(a.tm_year, a.tm_mon, a.tm_mday, a.tm_hour, a.tm_min, a.tm_sec) >
               std::tie(b.tm_year, b.tm_mon, b.tm_mday, b.tm_hour, b.tm_min, b.tm_sec);
    }

    // __DATE__ and __TIME__ as IDF records them: "Oct 14 2026" and "07:33:16".
    static bool parseBuildTime(const esp_app_desc_t& desc, std::tm& out) {
        char text[sizeof(desc.date) + sizeof(desc.time) + 2];
        std::snprintf(text, sizeof(text), "%.*s %.*s", static_cast<int>(sizeof(desc.date)), desc.date,
                      static_cast<int>(sizeof(desc.time)), desc.time);
        return strptime(text, "%b %d %Y %H:%M:%S", &out) != nullptr;
    }

    // Time in loop() before a pending image confirms itself, counted from the first
    // pass so the setup() splash cannot use it up.
    static constexpr unsigned long PENDING_IMAGE_CONFIRM_MS = 10000;

    static constexpr const char* TAG = "Esp32S3FirmwareStore";

    const esp_partition_t* runningPartition = nullptr;
    const esp_partition_t* updatePartition = nullptr;
    // 0 while no write is open: IDF never hands out 0, and refuses calls on a
    // handle it has already closed.
    esp_ota_handle_t otaHandle = 0;
    SimpleTimer confirmTimer;
    bool runningImagePending = false;
    bool guardingPendingImage = false;
};
