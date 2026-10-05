cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# PDN_DEVICE arrives from board_build.cmake_extra_args in platformio.ini and is
# the only per-environment channel into CMake. It names the project, which also
# names esp_app_desc_t.project_name -- useful in a boot log, and better than
# defaulting to whatever the checkout directory is called. Nothing reads it.
if(NOT DEFINED PDN_DEVICE)
    message(FATAL_ERROR
        "PDN_DEVICE is not set. Add -DPDN_DEVICE=pdn or -DPDN_DEVICE=fdn to "
        "board_build.cmake_extra_args for this environment.")
endif()

# u8g2 only enables its user pointer for unix and ARM-Linux builds, so on
# ESP-IDF the field is absent and a HAL has to reach its context some other
# way. Set globally rather than per-component on purpose: the field changes
# u8x8_t's layout, so u8g2's own sources and ours must agree or the offsets
# diverge silently.
idf_build_set_property(COMPILE_OPTIONS "-DU8X8_WITH_USER_PTR" APPEND)

project(${PDN_DEVICE})
