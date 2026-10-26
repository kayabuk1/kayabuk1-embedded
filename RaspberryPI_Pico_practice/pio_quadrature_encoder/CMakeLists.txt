# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pio_quadrature_encoder C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(pio_quadrature_encoder)

pico_generate_pio_header(pio_quadrature_encoder ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)

target_sources(pio_quadrature_encoder PRIVATE quadrature_encoder.c)

target_link_libraries(pio_quadrature_encoder PRIVATE
        pico_stdlib
        pico_multicore
        hardware_pio
        )

pico_enable_stdio_usb(pio_quadrature_encoder 1)

pico_add_extra_outputs(pio_quadrature_encoder)

# add url via pico_set_program_url
