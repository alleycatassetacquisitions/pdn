# build_src_filter has no effect under ESP-IDF; PlatformIO says so itself and
# then globs every file under src/. Device selection therefore lives here.
# src/cli is excluded by construction: only the named device's tree is globbed.
file(GLOB_RECURSE device_sources ${CMAKE_SOURCE_DIR}/src/${PDN_DEVICE}/*.cpp)

if(NOT device_sources)
    message(FATAL_ERROR "No sources found under src/${PDN_DEVICE}")
endif()

# "." is src/, matching -I src on the prebuilt path; ${PDN_DEVICE} is src/pdn or
# src/fdn, whose headers sit beside their own sources and are included by paths
# relative to it. lib/core and lib/esp32-drivers are not listed: PlatformIO
# still builds those as its own libraries and prepends their include dirs.
# REQUIRES is not optional here. Without it a component sees only IDF's common
# requirements, and these sources reach past them: esp_ota_* and esp_app_desc_t
# for firmware distribution, esp_core_dump_* for the crash logger, nvs for the
# preferences driver, esp_wifi for ESP-NOW. While Arduino is still in the build
# it is a public requirement of most of IDF and masks all of this, so the list
# below is what keeps the build honest about what it actually uses.
idf_component_register(
    SRCS ${device_sources}
    INCLUDE_DIRS "." "${PDN_DEVICE}"
    REQUIRES
        app_update
        driver
        esp_app_format
        esp_event
        esp_http_client
        esp_netif
        esp_partition
        esp_timer
        esp_wifi
        u8g2
        espcoredump
        mbedtls
        nvs_flash
)
