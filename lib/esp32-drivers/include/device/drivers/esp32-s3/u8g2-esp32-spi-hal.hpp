#pragma once

#include <cstring>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include "device/drivers/logger.hpp"
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <u8g2.h>

/**
 * SPI and GPIO backing for u8g2 on the ESP32-S3.
 *
 * Upstream u8g2 registers itself as an ESP-IDF component but ships no platform
 * HAL, and u8g2_Setup_*() takes these two callbacks directly. They are ours
 * rather than a third party's because the bus clock and SPI mode are exactly
 * what a panel gets tuned on, and both are read from u8g2's own display_info so
 * each panel keeps the values its driver declares. Reset timing is not read
 * here; u8g2's panel driver asks for it through U8X8_MSG_DELAY_MILLI and this
 * HAL obeys.
 */
struct U8g2Esp32SpiPins {
    gpio_num_t sclk;
    gpio_num_t mosi;
    gpio_num_t cs;
    gpio_num_t dc;
    gpio_num_t reset;
};

struct U8g2Esp32SpiContext {
    U8g2Esp32SpiPins pins;
    spi_host_device_t host;
    spi_device_handle_t device;
    uint8_t* dmaScratch;
};

/** The callback signature caps a single BYTE_SEND: u8x8_msg_cb takes its count
 * as a uint8_t. The bulk path is u8x8_cad_001, which forwards straight to this
 * callback rather than going through u8x8_byte.c. */
constexpr const char* U8G2_HAL_TAG = "U8g2Hal";
constexpr size_t U8G2_SPI_SCRATCH_BYTES = 256;

/**
 * Moves bytes to the panel and drives DC and CS around them.
 *
 * u8g2 hands over whichever buffer it has, and command sequences come from
 * .rodata, which is flash-mapped and cannot be a DMA source. The driver would
 * bounce such a buffer itself, allocating per transfer; the bytes go through one
 * long-lived scratch instead so the per-frame path never allocates.
 */
inline uint8_t u8g2Esp32SpiByteCallback(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr) {
    auto* context = static_cast<U8g2Esp32SpiContext*>(u8x8_GetUserPtr(u8x8));
    if (context == nullptr) {
        return 0;
    }

    switch (msg) {
        case U8X8_MSG_BYTE_SEND: {
            if (context->dmaScratch == nullptr) {
                LOG_E(U8G2_HAL_TAG, "BYTE_SEND with no scratch; BYTE_INIT must have failed");
                return 0;
            }
            // One transfer, never a loop: argInt is a uint8_t, so a single
            // BYTE_SEND is at most 255 bytes against a 256-byte scratch.
            if (argInt == 0) {
                break;
            }
            std::memcpy(context->dmaScratch, argPtr, argInt);
            spi_transaction_t transaction = {};
            transaction.length = static_cast<size_t>(argInt) * 8;
            transaction.tx_buffer = context->dmaScratch;
            if (spi_device_polling_transmit(context->device, &transaction) != ESP_OK) {
                LOG_E(U8G2_HAL_TAG, "polling transmit of %u bytes failed", (unsigned)argInt);
                return 0;
            }
            break;
        }
        case U8X8_MSG_BYTE_INIT: {
            spi_bus_config_t bus = {};
            bus.sclk_io_num = context->pins.sclk;
            bus.mosi_io_num = context->pins.mosi;
            bus.miso_io_num = -1;
            bus.quadwp_io_num = -1;
            bus.quadhd_io_num = -1;
            bus.max_transfer_sz = U8G2_SPI_SCRATCH_BYTES;
            if (spi_bus_initialize(context->host, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
                LOG_E(U8G2_HAL_TAG, "spi_bus_initialize failed on host %d", (int)context->host);
                return 0;
            }
            spi_device_interface_config_t device = {};
            // Both from the panel's own u8g2 driver, so each display keeps the
            // timing it declares instead of a number copied into this file.
            device.clock_speed_hz = static_cast<int>(u8x8->display_info->sck_clock_hz);
            device.mode = u8x8->display_info->spi_mode;
            device.spics_io_num = -1;  // CS is driven by hand, as u8g2 expects
            device.queue_size = 1;
            if (spi_bus_add_device(context->host, &device, &context->device) != ESP_OK) {
                LOG_E(U8G2_HAL_TAG, "spi_bus_add_device failed; the bus stays held");
                return 0;
            }
            context->dmaScratch = static_cast<uint8_t*>(
                heap_caps_malloc(U8G2_SPI_SCRATCH_BYTES, MALLOC_CAP_DMA));
            if (context->dmaScratch == nullptr) {
                LOG_E(U8G2_HAL_TAG, "could not allocate %u DMA-capable scratch bytes",
                      (unsigned)U8G2_SPI_SCRATCH_BYTES);
                return 0;
            }
            break;
        }
        case U8X8_MSG_BYTE_SET_DC:
            gpio_set_level(context->pins.dc, argInt);
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            gpio_set_level(context->pins.cs, u8x8->display_info->chip_enable_level);
            esp_rom_delay_us(1);
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            esp_rom_delay_us(1);
            gpio_set_level(context->pins.cs, u8x8->display_info->chip_disable_level);
            break;
        default:
            return 0;
    }
    return 1;
}

/** Releases what BYTE_INIT took: the scratch, the SPI device and the bus. Safe
 * to call when BYTE_INIT never ran. A BYTE_INIT that failed between
 * spi_bus_initialize and spi_bus_add_device leaves the bus held, because the
 * device handle is the only sentinel there is. */
inline void u8g2Esp32SpiRelease(U8g2Esp32SpiContext* context) {
    if (context == nullptr) {
        return;
    }
    if (context->dmaScratch != nullptr) {
        heap_caps_free(context->dmaScratch);
        context->dmaScratch = nullptr;
    }
    if (context->device != nullptr) {
        spi_bus_remove_device(context->device);
        context->device = nullptr;
        spi_bus_free(context->host);
    }
}

/** Configures the control lines and serves u8g2's delay and reset requests. */
inline uint8_t u8g2Esp32GpioAndDelayCallback(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr) {
    (void)argPtr;
    auto* context = static_cast<U8g2Esp32SpiContext*>(u8x8_GetUserPtr(u8x8));
    if (context == nullptr) {
        return 0;
    }

    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT: {
            gpio_config_t control = {};
            control.mode = GPIO_MODE_OUTPUT;
            control.pin_bit_mask = (1ULL << context->pins.cs) | (1ULL << context->pins.dc);
            if (context->pins.reset != GPIO_NUM_NC) {
                control.pin_bit_mask |= (1ULL << context->pins.reset);
            }
            gpio_config(&control);
            gpio_set_level(context->pins.cs, u8x8->display_info->chip_disable_level);
            gpio_set_level(context->pins.dc, 0);
            break;
        }
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(argInt));
            break;
        case U8X8_MSG_DELAY_10MICRO:
            esp_rom_delay_us(argInt * 10);
            break;
        case U8X8_MSG_DELAY_100NANO:
        case U8X8_MSG_DELAY_NANO:
            // Both are below the resolution of any delay the IDF offers; a single
            // microsecond is the shortest honest wait and is still correct,
            // since these are minimum setup times rather than exact ones.
            esp_rom_delay_us(1);
            break;
        case U8X8_MSG_GPIO_CS:
            gpio_set_level(context->pins.cs, argInt);
            break;
        case U8X8_MSG_GPIO_DC:
            gpio_set_level(context->pins.dc, argInt);
            break;
        case U8X8_MSG_GPIO_RESET:
            if (context->pins.reset != GPIO_NUM_NC) {
                gpio_set_level(context->pins.reset, argInt);
            }
            break;
        default:
            return 1;  // u8g2 polls for messages a HAL may legitimately ignore
    }
    return 1;
}
