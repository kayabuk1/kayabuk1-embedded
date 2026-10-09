// #include <stdio.h>
// #include "pico/stdlib.h"
// #include "hardware/spi.h"
// #include "misaki_font.h"

// // 74HC595をSPI機能を使わずに使う。
// // 次、SPI機能を使っていく。↑は一部コメントアウト

// #define PIN_MOSI 7
// #define PIN_SCK 6
// #define PIN_RCK 3

// #define MUX_A 20
// #define MUX_B 19
// #define MUX_C 18

// #define SPI_PORT spi0

// // -------------------------------------------------------------
// // ★ 市松模様（チェッカーボード）パターンデータ ★
// // カソード制御のため、ビットが 0 の場所が点灯します
// // -------------------------------------------------------------
// static const uint8_t pattern_A[8] = {
//     0xAA, // 0行目: 1 0 1 0 1 0 1 0 (0の列が点灯)
//     0x55, // 1行目: 0 1 0 1 0 1 0 1
//     0xAA, // 2行目: 1 0 1 0 1 0 1 0
//     0x55, // 3行目: 0 1 0 1 0 1 0 1
//     0xAA, // 4行目: 1 0 1 0 1 0 1 0
//     0x55, // 5行目: 0 1 0 1 0 1 0 1
//     0xAA, // 6行目: 1 0 1 0 1 0 1 0
//     0x55  // 7行目: 0 1 0 1 0 1 0 1
// };
// // -------------------------------------------------------------
// // パターンB (反転した市松模様)
// // -------------------------------------------------------------
// static const uint8_t pattern_B[8] = {
//     0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA
// };

// // -------------------------------------------------------------
// // 74HC138 (アノード行選択) 設定関数
// // 行番号 row (0〜7) をビット演算で MUX_A, MUX_B, MUX_C に出力
// // -------------------------------------------------------------
// static inline void set_mux_row(uint8_t row)
// {
//     gpio_put(MUX_A, (row >> 0) & 0x01); // 0ビット目
//     gpio_put(MUX_B, (row >> 1) & 0x01); // 1ビット目
//     gpio_put(MUX_C, (row >> 2) & 0x01); // 2ビット目
// // 例A：row = 3 の場合（2進数で 0 1 1）
// // MUX_A（0ビット目）の計算: (3 >> 0) & 0x01
// // 3 >> 0 ➔ シフトなし ➔ 2進数の 0 1 1
// // 011 & 001 ➔ 一番右の位同士だけ比較 ➔ 1 (HIGH)
// // MUX_B（1ビット目）の計算: (3 >> 1) & 0x01
// // 3 >> 1 ➔ 右に1つズラす ➔ 0 1 1 が 0 0 1 になる（端っこが押し出される）
// // 001 & 001 ➔ 一番右の位同士だけ比較 ➔ 1 (HIGH)
// // MUX_C（2ビット目）の計算: (3 >> 2) & 0x01
// // 3 >> 2 ➔ 右に2つズラす ➔ 0 1 1 が 0 0 0 になる
// // 000 & 001 ➔ 0 (LOW)
// // ➔ 結果: C=0, B=1, A=1 （2進数で 011 = 3）。「3」が出力
// }

// // -------------------------------------------------------------
// // 1行分の描画関数
// // -------------------------------------------------------------
// void display_line(uint8_t row, uint8_t line_data)
// {
//     // 1. SPIで74HC595へ列（カソード）データを送信
//     spi_write_blocking(SPI_PORT, &line_data, 1);

//     // 2. ラッチパルス(RCK)で出力を一括更新
//     gpio_put(PIN_RCK, 1);
//     sleep_us(1);
//     gpio_put(PIN_RCK, 0);

//     // 3. 74HC138で行（アノード）を選択
//     set_mux_row(row);
// }

// int main()
// {
//     stdio_init_all();

//     // SPI初期化 (100kHz)
//     spi_init(SPI_PORT, 100 * 1000);

//     // GPIO機能設定
//     gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
//     gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
//     gpio_set_function(PIN_RCK,  GPIO_FUNC_SIO);

//     gpio_set_function(MUX_A, GPIO_FUNC_SIO);
//     gpio_set_function(MUX_B, GPIO_FUNC_SIO);
//     gpio_set_function(MUX_C, GPIO_FUNC_SIO);

//     // 出力方向に設定
//     gpio_set_dir(PIN_RCK, GPIO_OUT);
//     gpio_set_dir(MUX_A,   GPIO_OUT);
//     gpio_set_dir(MUX_B,   GPIO_OUT);
//     gpio_set_dir(MUX_C,   GPIO_OUT);

//     gpio_put(PIN_RCK, 0);

//      // 最初はパターンAを指しておく
//     const uint8_t *p_pattern = pattern_A;
    
//     // 時間計測用の変数（ミリ秒）
//     uint32_t last_toggle_time = to_ms_since_boot(get_absolute_time());

// //     while (true)
// //     {   
// //          // -----------------------------------------------------
// //         // ★ 1秒(1000ms)経過したらポインタの切り替えを行う ★
// //         // -----------------------------------------------------
// //         uint32_t current_time = to_ms_since_boot(get_absolute_time());
// //         if (current_time - last_toggle_time >= 1000)
// //         {
// //             last_toggle_time = current_time;

// //             // p_pattern が A を指していれば B に、B であれば A に差し替える
// //             if (p_pattern == pattern_A) {
// //                 p_pattern = pattern_B;
// //             } else {
// //                 p_pattern = pattern_A;
// //             }
// //         }

// //         // 8行分の高速ダイナミック点灯スキャン（0行目〜7行目）
// //         for (uint8_t row = 0; row < 8; row++)
// //         {
// //             // ポインタを用いて該当行のデータを取得
// //             uint8_t line_data = *(p_pattern + row);
// // // 配列の名前は「先頭アドレス」を意味する
// // // p_pattern には、配列の0番目のデータがある**メモリの住所
// // // （アドレス番号：例 0x20001000）**が入っています。
// // // ポインタに整数を足すと「足した個数分だけ先の住所」になる
// // // C言語のルールでは、ポインタに + 1 や + row をすると、
// // // **「単純にアドレスの数値を1増やす」のではなく、
// // // 「そのデータ型（今回は1バイトの uint8_t）のサイズ×row 個分だけ
// // // 先の住所にジャンプする」**という仕組になっています。
// // // p_pattern[row] と完全に同じ意味

// //             // 1行描画
// //             display_line(row, line_data);

// //             // 1行あたりの表示保持時間 (1ms = 1000μs)
// //             // 全体で 1ms * 8行 = 8ms (約125Hz) で巡回
// //             sleep_us(1000);
// //         }
// //     }
//  while (true)
//     {
//         // ★ misaki_font.h に入っている全文字（TEXT_LENGTH 分）を順にループ
//         for (int char_idx = 0; char_idx < TEXT_LENGTH; char_idx++)
//         {
//             // 現在の文字の 8バイトドットデータへのポインタを取得
//             const uint8_t *p_pattern = font_text_list[char_idx];

//             // 1文字あたり約0.8秒間ダイナミック点灯（8ms × 100フレーム ＝ 800ms）
//             for (int frame = 0; frame < 100; frame++)
//             {
//                 // 8行分（0行目〜7行目）のスキャン
//                 for (uint8_t row = 0; row < 8; row++)
//                 {
//                     // ポインタ演算で該当行の 1バイトデータを取得
//                     uint8_t line_data = *(p_pattern + row);

//                     // 1行描画
//                     display_line(row, line_data);

//                     // 1行の表示時間 (1ms)
//                     sleep_us(1000);
//                 }
//             }
//         }
//     }
// }

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ★ 自動生成した美咲フォントヘッダファイルを読み込みます
#include "misaki_font.h"

// --- ピン割り当て定義 ---
#define PIN_MOSI 7   // 74HC595 SER (データ)
#define PIN_SCK  6   // 74HC595 SRCLK (クロック)
#define PIN_RCK  3   // 74HC595 RCLK (ラッチ)

#define MUX_OFF 21   // 74HC138 /G2A (ブランキング用：1=全消灯 / 0=点灯)
#define MUX_A   20   // 74HC138 A
#define MUX_B   19   // 74HC138 B
#define MUX_C   18   // 74HC138 C

#define SPI_PORT spi0

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
// 1行分の描画関数 (ブランキング制御付き)
// -------------------------------------------------------------
void hw_matrix_output_line(uint8_t row, uint8_t line_data)
{
    // 1. まず消灯させてデータ更新中の残像・ぼやけを完全にシャットアウト
    gpio_put(MUX_OFF, 1);

    // 2. SPIで74HC595へ列データ（カソード）を送信
    spi_write_blocking(SPI_PORT, &line_data, 1);

    // 3. ラッチパルス(RCK)を叩いて74HC595の出力を一括更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);

    // 4. 74HC138 で行（アノード）を選択
    set_mux_row(row);

    // 5. 点灯許可！
    gpio_put(MUX_OFF, 0);
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

    gpio_set_function(MUX_OFF, GPIO_FUNC_SIO);
    gpio_set_function(MUX_A,   GPIO_FUNC_SIO);
    gpio_set_function(MUX_B,   GPIO_FUNC_SIO);
    gpio_set_function(MUX_C,   GPIO_FUNC_SIO);

    // ピンの方向を出力に設定
    gpio_set_dir(PIN_RCK, GPIO_OUT);
    gpio_set_dir(MUX_OFF, GPIO_OUT);
    gpio_set_dir(MUX_A,   GPIO_OUT);
    gpio_set_dir(MUX_B,   GPIO_OUT);
    gpio_set_dir(MUX_C,   GPIO_OUT);

    // 初期状態：安全のため全消灯にしておく
    gpio_put(PIN_RCK, 0);
    gpio_put(MUX_OFF, 1);

    while (true)
    {
        // ★ misaki_font.h に入っている全文字（TEXT_LENGTH 分）を順にループ
        for (int char_idx = 0; char_idx < TEXT_LENGTH; char_idx++)
        {
            // 現在の文字の 8バイトドットデータへのポインタを取得
            const uint8_t *p_pattern = font_text_list[char_idx];

            // 1文字あたり約0.8秒間ダイナミック点灯（8ms × 100フレーム ＝ 800ms）
            for (int frame = 0; frame < 100; frame++)
            {
                // 8行分（0行目〜7行目）のスキャン
                for (uint8_t row = 0; row < 8; row++)
                {
                    // ポインタ演算で該当行の 1バイトデータを取得
                    // uint8_t line_data = *(p_pattern + row);
                    // ▼ 修正後（ ~ を付けてビット反転させる）
                    uint8_t line_data = ~(*(p_pattern + row));

                    // 1行描画
                    hw_matrix_output_line(row, line_data);

                    // 1行の表示時間 (1ms)
                    sleep_us(1000);
                }
            }
        }
    }
}