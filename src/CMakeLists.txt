# build_src_filter has no effect under ESP-IDF; PlatformIO says so itself and
# adds src/ as a component directory. Selecting which device's sources build is
# therefore this file's job, via the glob below.
# src/cli is excluded by construction: only the named device's tree is globbed.
# No CONFIGURE_DEPENDS: IDF includes component CMakeLists in script mode during
# its requirements pass, where CMake rejects the flag outright. A newly added
# source therefore needs a CMake re-run, and shows up as an undefined reference
# until it gets one.
file(GLOB_RECURSE device_sources ${CMAKE_SOURCE_DIR}/src/${PDN_DEVICE}/*.cpp)

if(NOT device_sources)
    message(FATAL_ERROR "No sources found under src/${PDN_DEVICE}")
endif()

# "." is src/; ${PDN_DEVICE} is src/pdn or
# src/fdn, whose headers sit beside their own sources and are included by paths
# relative to it. lib/core and lib/esp32-drivers are not listed: PlatformIO
# still builds those as its own libraries and prepends their include dirs.
# REQUIRES is not optional here. Without it a component sees only IDF's common
# requirements, and these sources reach past them: esp_core_dump_* for the crash
# logger, nvs for the preferences driver, esp_wifi for ESP-NOW, esp_http_client
# and esp_netif for uploads, and driver for UART/LEDC/RMT. The three managed
# components are injected by the component manager, so they are deliberately not
# listed here.
idf_component_register(
    SRCS ${device_sources}
    INCLUDE_DIRS "." "${PDN_DEVICE}"
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_netif
        esp_timer
        esp_wifi
        espcoredump
        nvs_flash
)
