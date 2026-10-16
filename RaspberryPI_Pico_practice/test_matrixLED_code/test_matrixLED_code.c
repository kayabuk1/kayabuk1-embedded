#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// --- ピン割り当て定義 ---
#define PIN_MOSI 7   // 74HC595 SER (データ)
#define PIN_SCK  6   // 74HC595 SRCLK (クロック)
#define PIN_RCK  3   // 74HC595 RCLK (ラッチ)
#define MUX_A   20   // 74HC138 A
#define MUX_B   19   // 74HC138 B
#define MUX_C   18   // 74HC138 C
#define SPI_PORT spi0

// 74HC138 (アノード) 指定行固定
static inline void select_row_static(uint8_t row) {
    gpio_put(MUX_A, (row >> 0) & 0x01);
    gpio_put(MUX_B, (row >> 1) & 0x01);
    gpio_put(MUX_C, (row >> 2) & 0x01);
}

// 74HC595 (カソード) 1バイト直接出力＆ラッチ
static void output_595_raw(uint8_t raw_data) {
    spi_write_blocking(SPI_PORT, &raw_data, 1);
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);
}

int main() {
    stdio_init_all();

    // SPI & GPIO 初期化
    spi_init(SPI_PORT, 100 * 1000);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_RCK,  GPIO_FUNC_SIO);
    gpio_set_function(MUX_A,    GPIO_FUNC_SIO);
    gpio_set_function(MUX_B,    GPIO_FUNC_SIO);
    gpio_set_function(MUX_C,    GPIO_FUNC_SIO);

    gpio_set_dir(PIN_RCK, GPIO_OUT);
    gpio_set_dir(MUX_A,   GPIO_OUT);
    gpio_set_dir(MUX_B,   GPIO_OUT);
    gpio_set_dir(MUX_C,   GPIO_OUT);

    gpio_put(PIN_RCK, 0);

    // アノード(74HC138)は 0行目(物理上端) を固定選択(通電)にしておく
    select_row_static(0);

    printf("=== 74HC595 Static Voltage Test Start ===\n");

    while (true) {
        // -------------------------------------------------------------
        // 【テスト①】 74HC595 出力を全 High 固定 (0xFF)
        // -------------------------------------------------------------
        // printf("[MODE 1] Output: ALL HIGH (0xFF) -> Check Q0~Q7 Voltage\n");
        // output_595_raw(0xFF);
        // // ★この5秒間に、74HC595の Q0〜Q7 出力ピンをテスターで測定してください。
        // //   理論値: 約 3.3V (5V電源なら約 5.0V)
        // sleep_ms(5000);

        // -------------------------------------------------------------
        // 【テスト②】 74HC595 出力を全 Low 固定 (0x00)
        // -------------------------------------------------------------
        printf("[MODE 2] Output: ALL LOW (0x00) -> Check Q0~Q7 Voltage\n");
        output_595_raw(0b10101010);
        // ★この5秒間に、74HC595の Q0〜Q7 出力ピンをテスターで測定してください。
        //   理論値: 0V (数mV〜数十mV)
        sleep_ms(5000);
    }

    return 0;
}
