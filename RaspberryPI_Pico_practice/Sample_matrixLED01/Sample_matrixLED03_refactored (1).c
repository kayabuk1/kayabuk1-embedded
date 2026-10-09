/**
 * @file Sample_matrixLED03_refactored.c
 * @brief Raspberry Pi Pico 8x8 LEDマトリクス制御プログラム（機能完全網羅・構造化リファクタリング版）
 * @details 74HC595(列カソード/SPI)および74HC138(行アノード/GPIO)を用いたダイナミック点灯制御ライブラリ
 */

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// フォントヘッダのインクルード
#include "misaki_font.h"

// --- ピン割り当て定義 ---
#define PIN_MOSI 7   // 74HC595 SER (データ)
#define PIN_SCK  6   // 74HC595 SRCLK (クロック)
#define PIN_RCK  3   // 74HC595 RCLK (ラッチ)
#define MUX_A   20   // 74HC138 A
#define MUX_B   19   // 74HC138 B
#define MUX_C   18   // 74HC138 C
#define SPI_PORT spi0

#define ON  true
#define OFF false

// 合成モード定義
#define OVERLAY_MODE_OR   0 /**< OR合成 (重ね合わせ) */
#define OVERLAY_MODE_AND  1 /**< AND合成 (マスク・和集合) */
#define OVERLAY_MODE_XOR  2 /**< XOR合成 (反転重ね合わせ) */

// -------------------------------------------------------------
// タイマー・周波数に関する定数定義 (500Hz 設定)
// -------------------------------------------------------------
#define ROW_SCAN_TIME_US           250                                  /**< 1行あたりの点灯時間 (250us) */
#define FRAME_SCAN_TIME_MS         ((8 * ROW_SCAN_TIME_US) / 1000)      /**< 1フレーム(8行)の走査時間 (2ms) */

#define DEFAULT_RENDER_DURATION_MS 10000 /**< デフォルト点灯保持時間 (10000ms = 10秒) */
#define DEFAULT_BLINK_INTERVAL_MS   300   /**< デフォルト点滅間隔 (300ms) */
#define DEFAULT_BLINK_TIMES        10    /**< デフォルト点滅回数 (10回) */
#define DEFAULT_SCROLL_STEP_MS     36    /**< デフォルト1ドット送り速度 (36ms) */

// -------------------------------------------------------------
// データ構造定義（共用体 union）
// -------------------------------------------------------------

/**
 * @brief 1行分(8ビット)のビットフィールド構造体
 */
typedef struct {
    uint8_t x0 : 1; /**< 0列目(左端) */
    uint8_t x1 : 1; /**< 1列目 */
    uint8_t x2 : 1; /**< 2列目 */
    uint8_t x3 : 1; /**< 3列目 */
    uint8_t x4 : 1; /**< 4列目 */
    uint8_t x5 : 1; /**< 5列目 */
    uint8_t x6 : 1; /**< 6列目 */
    uint8_t x7 : 1; /**< 7列目(右端) */
} RowBits;

/**
 * @brief 8x8 LEDフレームバッファ共用体
 * @details 8バイト一括(bytes)および (x,y)ビットアクセス(grid)を同位メモリで共有
 */
typedef union {
    uint8_t bytes[8]; /**< SPI送信・一括処理用 8バイト配列 */
    RowBits grid[8];  /**< (x, y) 座標ビットフィールド配列 */
} LEDBuffer;

/**
 * @brief グローバル・フレームバッファ（下書き用キャンバス）
 * @note ファイルスコープ static 変数として定義
 */
static LEDBuffer g_led_matrix = { .bytes = {0} };

/**
 * @brief スクロール表示用サンプルテキスト
 */
static const char scroll_text[] = " HELLO PICO! 12345 ";

// =============================================================
// 0. ハードウェア直接制御層 (HAL / ビット操作ヘルパー関数)
//    ※ GPIO/SPIを物理操作する低レイヤーおよびビット演算ユーティリティ
// =============================================================

/**
 * @brief 8ビットデータのLSB/MSBビット順逆転ユーティリティ
 * @param[in] b 対象の8ビットデータ
 * @return ビット順が前後反転した8ビットデータ
 */
static inline uint8_t reverse_bits(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4; // 4ビット単位（前半と後半）を入れ替え
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2; // 2ビットペアを入れ替え
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1; // 隣り合う1ビット同士を入れ替え
    return b;
}

/**
 * @brief 74HC138 (デコーダ/アノード行選択) 出力設定関数
 * @param[in] row 選択する行番号 (0 〜 7)
 */
static inline void hw_matrix_select_active_row(uint8_t row) {
    gpio_put(MUX_A, (row >> 0) & 0x01);
    gpio_put(MUX_B, (row >> 1) & 0x01);
    gpio_put(MUX_C, (row >> 2) & 0x01);
}

/**
 * @brief 1行分の物理信号出力関数（カソードデータSPI送信 ＋ アノード行有効化）
 * @param[in] row 出力対象の物理行番号 (0 〜 7)
 * @param[in] line_data 表示させる1行分(8ドット)の点灯パターン (1=ON, 0=OFF)
 * @note カソード制御のため、内部でビット反転(~)を行って送信します。
 */
static void hw_matrix_output_line(uint8_t row, uint8_t line_data) {
    // 74HC595(カソード)はLowで点灯するため反転
    uint8_t inv_data = ~line_data;
    spi_write_blocking(SPI_PORT, &inv_data, 1);

    // ラッチパルス(RCK)で74HC595の出力レジスタを更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);

    // 74HC138でアノード行を選択（通電・点灯開始）
    hw_matrix_select_active_row(row);
}

/**
 * @brief ハードウェア初期化処理
 */
void init_hardware(void) {
    stdio_init_all();

    // SPI初期化 (100kHz)
    spi_init(SPI_PORT, 100 * 1000);

    // GPIOピン機能設定
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_RCK,  GPIO_FUNC_SIO);
    gpio_set_function(MUX_A,    GPIO_FUNC_SIO);
    gpio_set_function(MUX_B,    GPIO_FUNC_SIO);
    gpio_set_function(MUX_C,    GPIO_FUNC_SIO);

    // ピン方向を出力に設定
    gpio_set_dir(PIN_RCK, GPIO_OUT);
    gpio_set_dir(MUX_A,   GPIO_OUT);
    gpio_set_dir(MUX_B,   GPIO_OUT);
    gpio_set_dir(MUX_C,   GPIO_OUT);

    gpio_put(PIN_RCK, 0);
}

// =============================================================
// 1. バッファ操作モジュール (プレフィックス: led_buffer_)
//    ※ SRAM上の「下書きキャンバス(g_led_matrix)」のみを変更するメモリ操作
// =============================================================

/**
 * @brief 1ピクセル(x, y)の点灯(ON)/消灯(OFF)をバッファに設定
 * @param[in] x X座標 (0 〜 7)
 * @param[in] y Y座標 (0 〜 7)
 * @param[in] on_off ON(true) または OFF(false)
 */
void led_buffer_set_pixel(uint8_t x, uint8_t y, bool on_off) {
    if (y >= 8 || x >= 8) return;
    switch (x) {
        case 0: g_led_matrix.grid[y].x0 = on_off; break;
        case 1: g_led_matrix.grid[y].x1 = on_off; break;
        case 2: g_led_matrix.grid[y].x2 = on_off; break;
        case 3: g_led_matrix.grid[y].x3 = on_off; break;
        case 4: g_led_matrix.grid[y].x4 = on_off; break;
        case 5: g_led_matrix.grid[y].x5 = on_off; break;
        case 6: g_led_matrix.grid[y].x6 = on_off; break;
        case 7: g_led_matrix.grid[y].x7 = on_off; break;
    }
}

/**
 * @brief 指定座標(x, y)のピクセル状態を取得
 * @param[in] x X座標 (0 〜 7)
 * @param[in] y Y座標 (0 〜 7)
 * @return 点灯していれば true, 消灯または範囲外なら false
 */
bool led_buffer_get_pixel(uint8_t x, uint8_t y) {
    if (y >= 8 || x >= 8) return false;
    switch (x) {
        case 0: return g_led_matrix.grid[y].x0;
        case 1: return g_led_matrix.grid[y].x1;
        case 2: return g_led_matrix.grid[y].x2;
        case 3: return g_led_matrix.grid[y].x3;
        case 4: return g_led_matrix.grid[y].x4;
        case 5: return g_led_matrix.grid[y].x5;
        case 6: return g_led_matrix.grid[y].x6;
        case 7: return g_led_matrix.grid[y].x7;
    }
    return false;
}

/**
 * @brief フレームバッファの全消灯（クリア）
 */
void led_buffer_clear_all(void) {
    for (int i = 0; i < 8; i++) {
        g_led_matrix.bytes[i] = 0x00;
    }
}

/**
 * @brief フレームバッファの全点灯（全ピクセルON）
 */
void led_buffer_fill_all(void) {
    for (int i = 0; i < 8; i++) {
        g_led_matrix.bytes[i] = 0xFF;
    }
}

/**
 * @brief フレームバッファの全画面白黒（点灯/消灯）反転
 */
void led_buffer_invert_all(void) {
    for (int i = 0; i < 8; i++) {
        g_led_matrix.bytes[i] = ~g_led_matrix.bytes[i];
    }
}

// =============================================================
// 2. 行・列（ライン）単位のバッファ操作
// =============================================================

/**
 * @brief 指定した1行(y行目)に8ビットデータを一括書き込み
 * @param[in] y 行番号 (0 〜 7)
 * @param[in] row_data 1行分の8ビットパターン
 */
void led_buffer_set_row(uint8_t y, uint8_t row_data) {
    if (y >= 8) return;
    g_led_matrix.bytes[y] = row_data;
}

/**
 * @brief 指定した1行(y行目)の8ビットデータを取得
 * @param[in] y 行番号 (0 〜 7)
 * @return 1行分の8ビットデータ
 */
uint8_t led_buffer_get_row(uint8_t y) {
    if (y >= 8) return 0;
    return g_led_matrix.bytes[y];
}

/**
 * @brief 指定した1列(x列目)に縦方向8ビットデータを一括書き込み
 * @param[in] x 列番号 (0 〜 7)
 * @param[in] col_data 縦1列分の8ビットパターン (bit0=y0 ... bit7=y7)
 */
void led_buffer_set_col(uint8_t x, uint8_t col_data) {
    if (x >= 8) return;
    for (uint8_t y = 0; y < 8; y++) {
        bool state = (col_data >> y) & 0x01;
        led_buffer_set_pixel(x, y, state);
    }
}

/**
 * @brief 指定した1列(x列目)の縦方向8ビットデータを取得
 * @param[in] x 列番号 (0 〜 7)
 * @return 縦1列分の8ビットデータ
 */
uint8_t led_buffer_get_col(uint8_t x) {
    if (x >= 8) return 0;
    uint8_t col_data = 0;
    for (uint8_t y = 0; y < 8; y++) {
        if (led_buffer_get_pixel(x, y)) {
            col_data |= (1 << y);
        }
    }
    return col_data;
}

// =============================================================
// 3. シフト（移動）バッファ操作
// =============================================================

/**
 * @brief 画面全体を上下に offset ピクセル分シフト（正＝上、負＝下）
 * @param[in] offset シフトドット数 (正の値で上シフト、負の値で下シフト)
 */
void led_buffer_shift_rows(int offset) {
    if (offset == 0) return;

    if (offset > 0) { // 上シフト (y=0方向へ移動、下端に0を補填)
        for (int y = 0; y < 8; y++) {
            if (y + offset < 8) {
                g_led_matrix.bytes[y] = g_led_matrix.bytes[y + offset];
            } else {
                g_led_matrix.bytes[y] = 0x00;
            }
        }
    } else { // 下シフト (y=7方向へ移動、上端に0を補填)
        int abs_offset = -offset;
        for (int y = 7; y >= 0; y--) {
            if (y - abs_offset >= 0) {
                g_led_matrix.bytes[y] = g_led_matrix.bytes[y - abs_offset];
            } else {
                g_led_matrix.bytes[y] = 0x00;
            }
        }
    }
}

/**
 * @brief 画面全体を左右に offset ピクセル分シフト（正＝左、負＝右）
 * @param[in] offset シフトドット数 (正の値で左シフト、負の値で右シフト)
 */
void led_buffer_shift_cols(int offset) {
    if (offset == 0) return;

    if (offset > 0) { // 左シフト (x0方向へ詰める)
        for (uint8_t y = 0; y < 8; y++) {
            g_led_matrix.bytes[y] = g_led_matrix.bytes[y] << offset;
        }
    } else { // 右シフト (x7方向へ詰める)
        int abs_offset = -offset;
        for (uint8_t y = 0; y < 8; y++) {
            g_led_matrix.bytes[y] = g_led_matrix.bytes[y] >> abs_offset;
        }
    }
}

/**
 * @brief 画面バッファ全体を1ピクセル左へ送り出し、右端列に新しい1列データを追加
 * @param[in] new_col_data 右端(x=7列目)に追加挿入する縦1列分(8ドット)のデータ
 */
void led_buffer_shift_left_col(uint8_t new_col_data) {
    for (uint8_t y = 0; y < 8; y++) {
        uint8_t new_bit = (new_col_data >> y) & 0x01;
        g_led_matrix.bytes[y] = (g_led_matrix.bytes[y] << 1) | new_bit;
    }
}

// =============================================================
// 4. 演出・重ね合わせ（合成）バッファ操作
// =============================================================

/**
 * @brief 指定した8x8ドット絵パターン(8バイト配列)を一括転写
 * @param[in] pattern 8バイトのパターン配列
 */
void led_buffer_draw_pattern(const uint8_t pattern[8]) {
    if (pattern == NULL) return;
    for (int i = 0; i < 8; i++) {
        g_led_matrix.bytes[i] = pattern[i];
    }
}

/**
 * @brief 指定した8x8ドット絵パターンを合成モード(OR/AND/XOR)で重ね合わせ
 * @param[in] pattern 8バイトのパターン配列
 * @param[in] mode 合成モード (OVERLAY_MODE_OR, OVERLAY_MODE_AND, OVERLAY_MODE_XOR)
 */
void led_buffer_overlay_pattern(const uint8_t pattern[8], uint8_t mode) {
    if (pattern == NULL) return;

    for (int i = 0; i < 8; i++) {
        switch (mode) {
            case OVERLAY_MODE_OR:
                g_led_matrix.bytes[i] |= pattern[i];
                break;
            case OVERLAY_MODE_AND:
                g_led_matrix.bytes[i] &= pattern[i];
                break;
            case OVERLAY_MODE_XOR:
                g_led_matrix.bytes[i] ^= pattern[i];
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 画面全体を90度時計回りに回転
 */
void led_buffer_rotate_90(void) {
    LEDBuffer temp = { .bytes = {0} };

    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            // (x, y) ピクセルを取得し、回転先 (7-y, x) へ設定
            bool bit = (g_led_matrix.bytes[y] >> x) & 0x01;
            if (bit) {
                temp.bytes[x] |= (1 << (7 - y));
            }
        }
    }

    g_led_matrix = temp;
}

// =============================================================
// 5. 描画・レンダラー制御モジュール (プレフィックス: led_render_)
//    ※ バッファの内容を500Hzのダイナミック点灯で物理LEDへ出力表示する
// =============================================================

/**
 * @brief 1画面分(0〜7行)を1周だけ高速走査して表示出力する関数（最小走査単位）
 */
void led_render_scan_once(void) {
    for (uint8_t row = 0; row < 8; row++) {
        uint8_t line_data = g_led_matrix.bytes[row];
        // 7 - row で上下物理配線を補正して出力
        hw_matrix_output_line(7 - row, line_data);
        sleep_us(ROW_SCAN_TIME_US);
    }
}

/**
 * @brief 現在のバッファ状態を指定ミリ秒間だけ静止・連続点灯表示する関数
 * @param[in] duration_ms 点灯保持時間（ミリ秒）。0指定時はデフォルト(10秒)
 */
void led_render_frame(uint32_t duration_ms) {
    if (duration_ms == 0) duration_ms = DEFAULT_RENDER_DURATION_MS;
    uint32_t elapsed_ms = 0;
    uint32_t step_ms = (FRAME_SCAN_TIME_MS > 0) ? FRAME_SCAN_TIME_MS : 1;

    while (elapsed_ms < duration_ms) {
        led_render_scan_once();
        elapsed_ms += step_ms;
    }
}

/**
 * @brief 指定した間隔・回数で画面バッファの内容を点滅させる関数
 * @param[in] interval_ms 点滅間隔（ミリ秒）。0指定時はデフォルト(300ms)
 * @param[in] times 点滅回数。0指定時はデフォルト(10回)
 */
void led_render_blink(uint32_t interval_ms, uint8_t times) {
    if (interval_ms == 0) interval_ms = DEFAULT_BLINK_INTERVAL_MS;
    if (times == 0)       times = DEFAULT_BLINK_TIMES;

    LEDBuffer backup = g_led_matrix;

    for (uint8_t i = 0; i < times; i++) {
        led_buffer_clear_all();
        led_render_frame(interval_ms);

        g_led_matrix = backup;
        led_render_frame(interval_ms);
    }
}

/**
 * @brief 指定した文字列を1ドットずつスムーズに左スクロール表示する高レベル表示関数
 * @param[in] text スクロール表示させる文字列(ASCII)
 * @param[in] step_delay_ms 1ピクセル移動ごとの静止保持時間 (ミリ秒)。0指定時はデフォルト(36ms)
 */
void led_render_scroll_text(const char *text, uint32_t step_delay_ms) {
    if (text == NULL) return;
    if (step_delay_ms == 0) step_delay_ms = DEFAULT_SCROLL_STEP_MS;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        const uint8_t *font_data = get_font_data_ascii(text[i]);

        for (int col = 0; col < 8; col++) {
            uint8_t col_pattern = 0;
            for (int row = 0; row < 8; row++) {
                uint8_t bit = (font_data[row] >> (7 - col)) & 0x01;
                col_pattern |= (bit << row);
            }

            led_buffer_shift_left_col(col_pattern);
            led_render_frame(step_delay_ms);
        }
    }
}

/**
 * @brief 電光掲示板モード（メッセージを常時無限ループスクロール表示）
 * @param[in] text 表示メッセージ文字列
 */
void mode_ticker_board(const char *text) {
    while (true) {
        led_render_scroll_text(text, DEFAULT_SCROLL_STEP_MS);
    }
}

// =============================================================
// 個別単体テストモジュール関数群
// =============================================================

/**
 * @brief 1ピクセル操作のテスト
 */
void test_pixel_control(void) {
    led_buffer_clear_all();
    led_buffer_set_pixel(0, 0, ON); // 左上
    led_buffer_set_pixel(7, 7, ON); // 右下
    led_render_frame(2000);
}

/**
 * @brief 累積打点テスト（点滅させずにポコポコとドットが増えていく）
 */
void test_accumulate_dots_animation(void) {
    led_buffer_clear_all();
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            led_buffer_set_pixel(x, y, ON);
            led_render_frame(50); // 50ms保持して次のドット追加
        }
    }
}

/**
 * @brief 単一1ドット掃引テスト（前のドットは消えて1つのドットが走る）
 */
void test_single_dot_sweep_animation(void) {
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            led_buffer_clear_all();
            led_buffer_set_pixel(x, y, ON);
            led_render_frame(50);
        }
    }
}

/**
 * @brief パターン描画・合成・90度回転テスト
 */
void test_pattern_and_rotation(void) {
    static const uint8_t heart_pattern[8] = {
        0b00000000,
        0b01100110,
        0b11111111,
        0b11111111,
        0b01111110,
        0b00111100,
        0b00011000,
        0b00000000
    };

    // 1. ハート型を描画
    led_buffer_draw_pattern(heart_pattern);
    led_render_frame(1500);

    // 2. 90度回転
    led_buffer_rotate_90();
    led_render_frame(1500);
}

/**
 * @brief 点滅制御のテスト
 */
void test_blink_control(void) {
    led_buffer_fill_all();
    led_render_frame(1000);
    led_render_blink(0, 0); // デフォルト点滅
}

/**
 * @brief 全単体テストの一括実行
 */
void run_all_unit_tests(void) {
    test_pixel_control();
    test_accumulate_dots_animation();
    test_single_dot_sweep_animation();
    test_pattern_and_rotation();
    test_blink_control();
}

// =============================================================
// メイン関数
// =============================================================
int main(void) {
    // 1. ハードウェア初期化
    init_hardware();

    // 2. 単体テスト実行
    run_all_unit_tests();

    // 3. メインループ：電光掲示板モード（常時点灯スクロール）
    mode_ticker_board(scroll_text);

    return 0;
}
