#include <stdio.h>
#include <wchar.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ★ 更新した美咲フォントヘッダをインクルード
#include "misaki_font.h"

// --- ピン割り当て定義 ---
#define PIN_MOSI 7   // 74HC595 SER (データ)
#define PIN_SCK  6   // 74HC595 SRCLK (クロック)
#define PIN_RCK  3   // 74HC595 RCLK (ラッチ)

#define MUX_A   20   // 74HC138 A
#define MUX_B   19   // 74HC138 B
#define MUX_C   18   // 74HC138 C

#define SPI_PORT spi0

// 表示させたいメッセージ（日本語・英数字ミックスOK！）
static const wchar_t scroll_text[] = L" Hello Pico! こんにちは 12345 ";

// -------------------------------------------------------------
// 74HC138 (アノード行選択) 設定関数
// -------------------------------------------------------------
static inline void set_mux_row(uint8_t row)
{
    gpio_put(MUX_A, (row >> 0) & 0x01); // 0ビット目
    gpio_put(MUX_B, (row >> 1) & 0x01); // 1ビット目
    gpio_put(MUX_C, (row >> 2) & 0x01); // 2ビット目
}

// -------------------------------------------------------------
// 1行分の描画関数 (MUX_OFF 削除版)
// -------------------------------------------------------------
void display_line(uint8_t row, uint8_t line_data)
{
    // カソード制御のためビット反転(~)
    uint8_t inv_data = ~line_data;
    spi_write_blocking(SPI_PORT, &inv_data, 1);

    // ラッチパルス(RCK)で出力を更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);

    // 行選択
    set_mux_row(row);
}

// -------------------------------------------------------------
// メイン関数
// -------------------------------------------------------------
int main()
{
    stdio_init_all();

    // SPI初期化 (100kHz)
    spi_init(SPI_PORT, 100 * 1000);

    // GPIOピン機能設定
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_RCK,  GPIO_FUNC_SIO);

    gpio_set_function(MUX_A, GPIO_FUNC_SIO);
    gpio_set_function(MUX_B, GPIO_FUNC_SIO);
    gpio_set_function(MUX_C, GPIO_FUNC_SIO);

    // ピンの方向を出力に設定
    gpio_set_dir(PIN_RCK, GPIO_OUT);
    gpio_set_dir(MUX_A,   GPIO_OUT);
    gpio_set_dir(MUX_B,   GPIO_OUT);
    gpio_set_dir(MUX_C,   GPIO_OUT);

    gpio_put(PIN_RCK, 0);

    // 現在画面に表示中の 8行分のフレームバッファ（初期状態は全消灯）
    uint8_t display_buffer[8] = {0};

    size_t text_length = wcslen(scroll_text);

    while (true)
    {
        // 1文字ずつ順番に取り出す
        for (size_t char_idx = 0; char_idx < text_length; char_idx++)
        {
            // 文字コードからフォントデータ（8バイト）を取得
            const uint8_t *next_font = get_font_data(scroll_text[char_idx]);

            // 1文字（8列）分、1ドットずつ左へシフト
            for (int col = 0; col < 8; col++)
            {
                // 各行（0〜7行）のビットを左へ1ピクセル移動し、新しいドットを右端に追加
                for (int row = 0; row < 8; row++)
                {
                    uint8_t new_bit = (next_font[row] >> (7 - col)) & 0x01;
                    display_buffer[row] = (display_buffer[row] << 1) | new_bit;
                }

                // 1ドットシフトした状態でダイナミック点灯（表示スピード調整）
                for (int frame = 0; frame < 6; frame++)
                {
                    for (uint8_t row = 0; row < 8; row++)
                    {
                        display_line(row, display_buffer[row]);
                        sleep_us(2000); // 1ms待機
                    }
                }
            }
        }
    }
}