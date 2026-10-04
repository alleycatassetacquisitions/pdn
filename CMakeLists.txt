cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# PDN_DEVICE arrives from board_build.cmake_extra_args in platformio.ini and is
# the only per-environment channel into CMake. It names the project, so it also
# sets esp_app_desc_t.project_name, which is what tells a receiver whether an
# image was built for its own device family. Left to default, the name would be
# whatever the checkout directory is called.
if(NOT DEFINED PDN_DEVICE)
    message(FATAL_ERROR
        "PDN_DEVICE is not set. Add -DPDN_DEVICE=pdn or -DPDN_DEVICE=fdn to "
        "board_build.cmake_extra_args for this environment.")
endif()

project(${PDN_DEVICE})
