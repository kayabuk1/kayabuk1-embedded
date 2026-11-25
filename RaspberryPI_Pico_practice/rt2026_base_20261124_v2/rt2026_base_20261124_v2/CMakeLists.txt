# RT2026 基礎プログラム（Raspberry Pi Pico 2）
#
# VS Code の Raspberry Pi Pico 拡張機能で新しいプロジェクトを作った場合は、
# 拡張機能が作った CMakeLists.txt の先頭（「DO NOT EDIT」と書かれた部分）を残し、
# project(...) から下を、このファイルの project(...) から下に置き換えてください。

# Generated Cmake Pico project file
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# 使用するボード（Pico 2 / RP2350）
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(rt2026_base C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# 実行ファイルとソース一覧
add_executable(rt2026_base
    main.c
    control.c
    line_sensor.c
    selftest.c
    at25sf/at25sf.c
    flash_log/flash_log.c
    adc_mcp3208or3008/adc_mcp3208or3008.c
    motor/motor.c
    encoder/encoder.c
    gyro/gyro.c
    buzzer/buzzer.c
    sw/sw.c
)

# エンコーダのPIOのプログラムから、quadrature_encoder.pio.h を自動生成
pico_generate_pio_header(rt2026_base ${CMAKE_CURRENT_LIST_DIR}/encoder/quadrature_encoder.pio)

# board.h などを "board.h"、ドライバを "motor/motor.h" の形で読めるようにします。
# at25sf と flash_log は、中で "at25sf.h" の形で読んでいるので、フォルダも足しています。
target_include_directories(rt2026_base PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/at25sf
    ${CMAKE_CURRENT_LIST_DIR}/flash_log
)

# フラッシュメモリのピンを、機体の基板のもの（SPI0、GP2〜GP5）にします
target_compile_definitions(rt2026_base PRIVATE PICO2350=1)

target_compile_options(rt2026_base PRIVATE -Wall)

# リンクするハードウェアライブラリ
target_link_libraries(rt2026_base
    pico_stdlib
    hardware_spi
    hardware_pwm
    hardware_pio
    hardware_adc
    hardware_clocks
    hardware_watchdog
)

# printf の出力先設定（USBシリアル有効、UART無効）
pico_enable_stdio_usb(rt2026_base 1)
pico_enable_stdio_uart(rt2026_base 0)

# uf2 などの生成
pico_add_extra_outputs(rt2026_base)