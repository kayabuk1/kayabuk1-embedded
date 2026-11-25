# RT2026 基礎プログラム（機体の基板 = Raspberry Pi Pico 2、教育用の実験基板 = Raspberry Pi Pico）
#
# VS Code の Raspberry Pi Pico 拡張機能で新しいプロジェクトを作った場合は、
# 拡張機能が作った CMakeLists.txt の先頭（「DO NOT EDIT」と書かれた部分）を残し、
# project(...) から下を、このファイルの project(...) から下に置き換えてください。
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

# 使うマイコンのボード。ここを変えると、ピンの設定（board.h の BOARD_EDU と、下の PICO2350）も一緒に切り替わります。
#   pico2 : 機体の基板（Pico 2）
#   pico  : 教育用の実験基板（Pico）とブレッドボード
# 変えた後は、ビルドのフォルダ（build）を消してから、ビルドし直します。
# VS Code の拡張機能が作った先頭の部分にも同じ行があるときは、そちらを変えます（先に書かれた方が使われます）。
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(rt2026_base C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(rt2026_base
    main.c
    control.c
    line_sensor.c
    selftest.c
    at25sf/at25sf.c
    flash_log/flash_log.c
    adc_mcp3208or3008/adc_mcp3208or3008.c
    encoded_geared_motor/encoded_geared_motor.c
    gyro/gyro.c
    l3gd20h/l3gd20h.c
    buzzer/buzzer.c
    sw/sw.c
)

# エンコーダのPIOのプログラムから、quadrature_encoder.pio.h を作ります
pico_generate_pio_header(rt2026_base ${CMAKE_CURRENT_LIST_DIR}/encoded_geared_motor/quadrature_encoder.pio)

# board.h などを "board.h"、ドライバを "gyro/gyro.h" の形で読めるようにします。
# at25sf と flash_log は、中で "at25sf.h" の形で読んでいるので、フォルダも足しています。
target_include_directories(rt2026_base PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/at25sf
    ${CMAKE_CURRENT_LIST_DIR}/flash_log
)

# フラッシュメモリのピンを、マイコンに合わせて切り替えます（at25sf/at25sf.h の PICO2350）。
#   Pico 2（機体の基板）       : SPI0、SCK=GP2、MOSI=GP3、MISO=GP4、CS=GP5
#   Pico（教育用の実験基板）   : SPI0、SCK=GP6、MOSI=GP7、MISO=GP4、CS=GP3
if(PICO_PLATFORM MATCHES "rp2350")
    target_compile_definitions(rt2026_base PRIVATE PICO2350=1)
else()
    target_compile_definitions(rt2026_base PRIVATE PICO2350=0)
endif()

target_compile_options(rt2026_base PRIVATE -Wall)

target_link_libraries(rt2026_base
    pico_stdlib
    hardware_spi
    hardware_pwm
    hardware_pio
    hardware_adc
    hardware_clocks
    hardware_watchdog
)

# printf は USBシリアルに出します
pico_enable_stdio_usb(rt2026_base 1)
pico_enable_stdio_uart(rt2026_base 0)

pico_add_extra_outputs(rt2026_base)
