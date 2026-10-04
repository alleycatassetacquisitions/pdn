#pragma once

#include <cstring>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <u8g2.h>

/**
 * SPI and GPIO backing for u8g2 on the ESP32-S3.
 *
 * Upstream u8g2 registers itself as an ESP-IDF component but ships no platform
 * HAL: supplying these two callbacks was the Arduino library's job, and
 * u8g2_Setup_*() takes them directly. They are deliberately ours rather than a
 * third party's, because the bus clock and the reset timing are exactly what a
 * panel gets tuned on, and both are read from u8g2's own display_info so each
 * panel keeps the values its driver declares.
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

/** Largest BYTE_SEND u8g2 issues is one tile row; 256 leaves headroom. */
constexpr size_t U8G2_SPI_SCRATCH_BYTES = 256;

/**
 * Moves bytes to the panel and drives DC and CS around them.
 *
 * The caller's buffer lives inside the u8g2 object, so it is copied through a
 * DMA-capable scratch rather than handed to the driver directly: with
 * CONFIG_SPIRAM_USE_MALLOC enabled, where an allocation lands depends on its
 * size, and a transfer from PSRAM would fail rather than merely be slow.
 */
inline uint8_t u8g2Esp32SpiByteCallback(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr) {
    auto* context = static_cast<U8g2Esp32SpiContext*>(u8x8_GetUserPtr(u8x8));
    if (context == nullptr) {
        return 0;
    }

    switch (msg) {
        case U8X8_MSG_BYTE_SEND: {
            const auto* source = static_cast<const uint8_t*>(argPtr);
            size_t remaining = argInt;
            while (remaining > 0) {
                size_t chunk = remaining < U8G2_SPI_SCRATCH_BYTES ? remaining : U8G2_SPI_SCRATCH_BYTES;
                std::memcpy(context->dmaScratch, source, chunk);
                spi_transaction_t transaction = {};
                transaction.length = chunk * 8;
                transaction.tx_buffer = context->dmaScratch;
                if (spi_device_polling_transmit(context->device, &transaction) != ESP_OK) {
                    return 0;
                }
                source += chunk;
                remaining -= chunk;
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
                return 0;
            }
            context->dmaScratch = static_cast<uint8_t*>(
                heap_caps_malloc(U8G2_SPI_SCRATCH_BYTES, MALLOC_CAP_DMA));
            if (context->dmaScratch == nullptr) {
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
            // Below the resolution of any delay the IDF offers; a single
            // microsecond is the shortest honest wait and is still correct,
            // since these are minimum setup times rather than exact ones.
            esp_rom_delay_us(1);
            break;
        case U8X8_MSG_DELAY_NANO:
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
