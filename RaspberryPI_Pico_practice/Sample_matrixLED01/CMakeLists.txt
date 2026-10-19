cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ====================================================================================
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

include(pico_sdk_import.cmake)

project(Sample_matrixLED02 C CXX ASM)

pico_sdk_init()

# ----------------------------------------------------
# 1つ目のプログラム: Sample_matrixLED03
# ----------------------------------------------------
add_executable(Sample_matrixLED03
    Sample_matrixLED03.c
)
pico_set_program_name(Sample_matrixLED03 "Sample_matrixLED03")
pico_set_program_version(Sample_matrixLED03 "0.1")
pico_enable_stdio_uart(Sample_matrixLED03 0)
pico_enable_stdio_usb(Sample_matrixLED03 1)

target_link_libraries(Sample_matrixLED03
    pico_stdlib
    hardware_spi
)
target_include_directories(Sample_matrixLED03 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
pico_add_extra_outputs(Sample_matrixLED03)

# ----------------------------------------------------
# 2つ目のプログラム: Sample_matrixLED03_doc
# ----------------------------------------------------
add_executable(Sample_matrixLED03_doc
    Sample_matrixLED03_doc.c
)
target_compile_options(Sample_matrixLED03_doc PRIVATE -Wall -Wextra)
pico_set_program_name(Sample_matrixLED03_doc "Sample_matrixLED03_doc")
pico_set_program_version(Sample_matrixLED03_doc "0.1")
pico_enable_stdio_uart(Sample_matrixLED03_doc 0)
pico_enable_stdio_usb(Sample_matrixLED03_doc 1)

target_link_libraries(Sample_matrixLED03_doc
    pico_stdlib
    hardware_spi
)
target_include_directories(Sample_matrixLED03_doc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
pico_add_extra_outputs(Sample_matrixLED03_doc)
