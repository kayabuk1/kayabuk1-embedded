/**
 * @file Sample_matrixLED03_refactored.c
 * @brief Raspberry Pi Pico 8x8 LEDマトリクス制御プログラム
 * @details 74HC595(列カソード/SPI)および74HC138(行アノード/GPIO)を
 * 用いたダイナミック点灯制御ライブラリ
 */
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// 美咲フォントヘッダのインクルード
#include "misaki_font.h"

// --- ピン割り当て定義 ---
#define PIN_MOSI 7   // 74HC595 SER (データ)
#define PIN_SCK  6   // 74HC595 SRCLK (クロック)
#define PIN_RCK  3   // 74HC595 RCLK (ラッチ)
#define DMUX_A   20   // 74HC138 A
#define DMUX_B   19   // 74HC138 B
#define DMUX_C   18   // 74HC138 C
#define SPI_PORT spi0

#define ON true
#define OFF false

// 合成モード定義
#define OVERLAY_MODE_OR   0 /**< OR合成 (重ね合わせ) */
#define OVERLAY_MODE_AND  1 /**< AND合成 (マスク・積集合) */
#define OVERLAY_MODE_XOR  2 /**< XOR合成 (反転重ね合わせ) */

// -------------------------------------------------------------
// タイマー・周波数に関する定数定義 (500Hz 設定)
// -------------------------------------------------------------
// ●1フレーム（8行）にかかる時間 
// 　1秒(1,000,000μs) ÷ 500Hz = 2,000μs ＝ 2ms
// ●1行あたりの切り替え時間（sleep_us の値）
// 　2,000μs ÷ 8行 = 250μs
#define ROW_SCAN_TIME_US 250 /**< 1行あたりの点灯時間 (250us) */

// デフォルト時間設定
#define DEFAULT_RENDER_DURATION_MS 10000 /**< デフォルト点灯保持時間 (10000ms = 10秒) */
#define DEFAULT_BLINK_INTERVAL_MS   300   /**< デフォルト点滅間隔 (300ms) */
#define DEFAULT_BLINK_TIMES        10    /**< デフォルト点滅回数 (10回) */
#define DEFAULT_SCROLL_STEP_MS     360    /**< デフォルト1ドット送り速度 (360ms) */

// -------------------------------------------------------------
// データ構造定義（共用体 union）
// -------------------------------------------------------------
// 原点 (0, 0)：画面の左上隅。
// ■データ構造（共用体 union の活用）
// union（共用体）を使うと、「8バイトの配列」としてもアクセスでき、
// 同時に「(y, x) 座標の1ビット」としても直感的にアクセスできる
// 構造体を作ることができる。
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
 * @details 8バイト一括(bytes)および (x,y)ビットアクセス(grid)を同じメモリで共有
 */
typedef union {
    uint8_t bytes[8]; /**< SPI送信・一括処理用の8バイト配列 */
    RowBits grid[8]; /**< (x, y) 座標ビットフィールド配列。grid[y].x0 〜 grid.x7 で1ビットずつ直接アクセスする */
} LEDBuffer_t;
// マトリクスLED左上が原点（型番印刷側面を下とした時）
//     左上 x 0     1    2    3    4    5    6    7
// bytes[y]0 [0].x0,[0].x1
// bytes[y]1 [1].x0,[1].x1...
// bytes[y]2
// bytes[y]3
// bytes[y]4
// bytes[y]5
// bytes[y]6
// bytes[y]7

/**
 * @brief グローバル・フレームバッファ（下書き用キャンバス）の宣言と初期化
 */
static LEDBuffer_t led_matrix_buffer = { .bytes = {0} };

/**
 * @brief スクロール表示用サンプルテキスト※表示させたい文字列を入れる配列
 * @note ■ ASCIIコード版
 * @typed char
 */
static const char scroll_text[] = " HELLO PICO! 12345 ";
/**
 * @brief スクロール表示用サンプルテキスト
 * @note ■ UTF-8 使う場合は ASCII版をコメントアウトしてこちらを使用
 * @typed wchar_t　※型が異なるので注意
 */
// static const wchar_t scroll_text[] = L" Hello Pico! こんにちは！";
// ※表示させたいメッセージ（日本語・英数字ミックスOK）
// ●C言語コンパイラに「文字」は「数値（文字コード）」に変換されるので、そのまま文字列を入れる。

// =============================================================
// 0. ハードウェア直接制御層 (HAL / 内部ヘルパー関数)
//    ※ GPIOやSPIを直接叩いてIC(74HC138/595)を物理操作する低レイヤー
// =============================================================

static inline uint8_t reverse_bits(uint8_t b);
static inline void hw_matrix_led_blanking(void);
static inline void hw_matrix_select_active_row(uint8_t row);       
static void        hw_matrix_output_line(uint8_t row, uint8_t line_data); 

/**
 * @brief 8ビットデータのLSD/MSBビット順逆転関数
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
 * @brief 74HC595(カソード)へ全High(0xFF)を出力(low点灯high消灯)し、
 *　LEDを消灯(ブランキング)する
 * @note 行切り替え時のゴースト(残像)を防止するために使用します。
 *　※バッファクリア関数は描画用の下書きをクリアする関数なので役割が違う。
 */
static inline void hw_matrix_led_blanking(void){
    uint8_t clear_data = 0xFF;
    spi_write_blocking(SPI_PORT, &clear_data, 1);
     // ラッチパルス(RCK)で出力を更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);
}
// 没hw_matrix_select_active_row(row)（74HC138のGPIOピン操作） も毎回実行無駄な為
// static inline void hw_matrix_led_blanking(uint8_t ago_row){
//     uint8_t clear_line_data = 0x00;
//     hw_matrix_output_line(uint8_t ago_row, uint8_t clear_line_data);
// }

/**
 * @brief 74HC138 (デコーダ/アノード行選択※点灯行選択) 出力設定関数
 * @param[in] row 選択する行番号 (0 〜 7)
 */
static inline void hw_matrix_select_active_row(uint8_t row)
{
    gpio_put(DMUX_A, (row >> 0) & 0x01); // 0ビット目
    gpio_put(DMUX_B, (row >> 1) & 0x01); // 1ビット目
    gpio_put(DMUX_C, (row >> 2) & 0x01); // 2ビット目
    // ●74HC138との連携：74HC138は、3本のバイナリ入力端子(A,B,C)の「H/L」の
    // 組み合わせで8本の出力端子(Y0~Y7)のどれか「１つ」に「Low」を出力する、
}

/**
 * @brief 1行分の物理信号出力関数（カソードデータSPI送信 ＋ アノード行有効化）
 * @param[in] row 出力対象の物理行番号 (0 〜 7)
 * @param[in] line_data 表示させる1行分(8ドット)の点灯パターン (1=ON, 0=OFF)
 * @note カソード制御のため、内部でビット反転(~)を行って送信します。
 */
void hw_matrix_output_line(uint8_t row, uint8_t line_data)
{   
    // ●uint8_t row: 表示対象の行番号（0 〜 7）。
    // uint8_t line_data: その行に表示させたい 1行分（8ドット）のLED点灯パターン。
    // 元データでは、1 ＝ 点灯（ON）、0 ＝ 消灯（OFF）という直感的なデータになっている。

    // 先に74HC138でアノード行を選択（点灯はまだ）
    hw_matrix_select_active_row(row);

    uint8_t inv_data = ~line_data;
    spi_write_blocking(SPI_PORT, &inv_data, 1);
    // ●int spi_write_blocking
        // (spi_inst_t *spi, const uint8_t *src, size_t len)
    // ●spi_inst_t: Pico SDKで定義されている「SPIペリフェラル（ハードウェア機能）
    // を管理する構造体」 の型名。RP2040マイコン内部のSPI回路のレジスタ群が
    // まとめられている。
    // ●len (Length): src のアドレスから「何バイト分を順番に送るか」
    // というデータ長指定。
    // 今回は 1 バイト（8ビット）送るため 1 を指定。
    // もし 10 バイトの配列をまとめて送りたければ 10 を指定。

    // ラッチパルス(RCK)で出力を更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);
}


/**
 * @brief ハードウェア初期化処理
 */
void init_hardware(void) {
    stdio_init_all();

    // SPI初期化 (100kHz⇒1000kHzに変更)
    spi_init(SPI_PORT, 1000 * 1000);

    // SPI通信(74HC595)用ピン機能設定
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI); // 74HC595 SER (データ)※データ入力
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI); // 74HC595 SRCLK (クロック)※データシフト
    gpio_set_function(PIN_RCK,  GPIO_FUNC_SIO); // 74HC595 RCK (ラッチ)※データ確定

    // 74HC138 DMUXピン機能設定
    gpio_set_function(DMUX_A,    GPIO_FUNC_SIO); // 74HC138 A
    gpio_set_function(DMUX_B,    GPIO_FUNC_SIO); // 74HC138 B
    gpio_set_function(DMUX_C,    GPIO_FUNC_SIO); // 74HC138 C

    // ピン方向を出力に設定
    gpio_set_dir(PIN_RCK, GPIO_OUT);
    gpio_set_dir(DMUX_A,   GPIO_OUT);
    gpio_set_dir(DMUX_B,   GPIO_OUT);
    gpio_set_dir(DMUX_C,   GPIO_OUT);

    gpio_put(PIN_RCK, 0);
}

// -------------------------------------------------------------
// １．バッファ操作（ゲッター・セッター、全画操作など）モジュール
// 　　※下書きキャンバス(led_matrix_buffer)」のみを変更するメモリ操作
// -------------------------------------------------------------

void led_buffer_set_pixel(uint8_t x, uint8_t y, bool on_off);
bool led_buffer_get_pixel(uint8_t x, uint8_t y);
void led_buffer_clear_all(void);

/**
 * @brief 1ピクセル(x, y)の点灯(ON)/消灯(OFF)をバッファにセットする関数
 * @param[in] x X座標 (0 〜 7)
 * @param[in] y Y座標 (0 〜 7)
 * @param[in] on_off ON(true) または OFF(false)
 */
void led_buffer_set_pixel(uint8_t x, uint8_t y, bool on_off){
    // 範囲外ガード（0〜7以外の値が入ってきたら何もしない）
    if (y >= 8 || x >= 8) return;
    // ビットフィールドメンバには配列に
    // x の値（0〜7）に応じて、対応するビットフィールドメンバに直接代入
    switch (x) {
        case 0: led_matrix_buffer.grid[y].x0 = on_off; break;
        case 1: led_matrix_buffer.grid[y].x1 = on_off; break;
        case 2: led_matrix_buffer.grid[y].x2 = on_off; break;
        case 3: led_matrix_buffer.grid[y].x3 = on_off; break;
        case 4: led_matrix_buffer.grid[y].x4 = on_off; break;
        case 5: led_matrix_buffer.grid[y].x5 = on_off; break;
        case 6: led_matrix_buffer.grid[y].x6 = on_off; break;
        case 7: led_matrix_buffer.grid[y].x7 = on_off; break;
    }
}

/**
 * @brief 指定座標(x, y)のピクセル状態を取得
 * @param[in] x X座標 (0 〜 7)
 * @param[in] y Y座標 (0 〜 7)
 * @return 点灯していれば true, 消灯または範囲外なら false
 */
bool led_buffer_get_pixel(uint8_t x, uint8_t y){
    if (y >= 8 || x >= 8) return false;
    // 戻り値の型が boolのため、早期リターンも何かboolを返す必要がある。
    switch (x) {
        case 0: return led_matrix_buffer.grid[y].x0;
        case 1: return led_matrix_buffer.grid[y].x1;
        case 2: return led_matrix_buffer.grid[y].x2;
        case 3: return led_matrix_buffer.grid[y].x3;
        case 4: return led_matrix_buffer.grid[y].x4;
        case 5: return led_matrix_buffer.grid[y].x5;
        case 6: return led_matrix_buffer.grid[y].x6;
        case 7: return led_matrix_buffer.grid[y].x7;
        // stdbool.h の設定で関数の戻り値型が bool の場合、
        // 0 は false、1（非ゼロ）は true に自動的に変換される。
        // x0（1ビット）しか入っていないので自動的にtrue/falseになる。
    }
    return false; // ここに来ることはないが、コンパイラ警告回避のために追加
}


/**
 * @brief フレームバッファの全消灯（クリア）
 */
void led_buffer_clear_all(void){
     for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = 0x00; //0b0000 0000
        // 共有体の8ビットごと扱えるbytes[]でアクセスして全部0に。
    }
}

/**
 * @brief フレームバッファの全点灯（全ピクセルON）
 */
void led_buffer_fill_all(void){
     for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = 0xFF; // 0b1111 1111
    }
}

/**
 * @brief フレームバッファの全画面白黒（点灯/消灯）反転
 */
void led_buffer_invert_all(void){
     for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = ~led_matrix_buffer.bytes[i]; 
        // 全ビット反転
    }
}

// -------------------------------------------------------------
// ２．行・列（ライン）単位のバッファ操作
// -------------------------------------------------------------
void led_buffer_set_row(uint8_t y, uint8_t row_data); //指定した1行に8ビットデータを書き込み
uint8_t led_buffer_get_row(uint8_t y); //指定した1行の8ビットデータを取得
void led_buffer_set_col(uint8_t x, uint8_t col_data);  //指定した1列に8ビットデータを縦方向に書き込み
uint8_t led_buffer_get_col(uint8_t x); //指定した1列の8ビットデータを取得

/**
 * @brief 指定した1行(y行目)に8ビットデータを一括書き込み
 * @param[in] y 行番号 (0 〜 7)
 * @param[in] row_data 1行分の8ビットパターン ex0b1010_1111
 */
void led_buffer_set_row(uint8_t y, uint8_t row_data) {
    if (y >= 8) return;
    led_matrix_buffer.bytes[y] = row_data;
}

/**
 * @brief 指定した1行(y行目)の8ビットデータを取得
 * @param[in] y 行番号 (0 〜 7)
 * @return 1行分の8ビットデータ
 */
uint8_t led_buffer_get_row(uint8_t y) {
    if (y >= 8) return 0;
    return led_matrix_buffer.bytes[y];
}

/**
 * @brief 指定した1列(x列目)に縦方向8ビットデータを一括書き込み
 * @param[in] x 列番号 (0 〜 7)
 * @param[in] col_data 縦1列分の8ビットパターン (bit0=y0x0... bit7=y7x0)
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
            // LEDブッファの1ピクセルの状態が点灯ならif内処理
            col_data |= (1 << y);
        }
    }
    return col_data;
}

// -------------------------------------------------------------
// ３．シフト（移動）バッファ操作
// -------------------------------------------------------------
void led_buffer_shift_cols(int offset_times); 
// 画面全体を左右に offset_times ピクセル分シフト（正＝右、負＝左）
void led_buffer_shift_rows(int offset_times); 
// 画面全体を上下に offset_times ピクセル分シフト（正＝上、負＝下）
void led_buffer_push_col_from_right(uint8_t new_rightend_col_data);
void led_buffer_push_col_from_left(uint8_t new_leftend_col_data);

// 【画面上の配置】   [x0:左端]  [x1]  [x2]  [x3]  [x4]  [x5]  [x6]  [x7:右端]
//                     │       │     │     │     │     │     │       │
// 【C言語のビット】  bit0    bit1　bit2  bit3  bit4  bit5  bit6  bit7 (MSB)
//                   (LSB/左)                                     (MSB/右)
//  0b1 0 1 0 1 0 1 0 実際のビットの並びとLED画面への反映が反転しているので注意。
//bit 7|6|5|4|3|2|1|0 番目
/**
 * @brief 画面全体を左右に offset_times ピクセル分シフト（正＝右へ、負＝左へ※左上原点）
 * @param[in] offset_times シフトドット数 (正の値で右シフト、負の値で左シフト)
 */
void led_buffer_shift_cols(int offset_times) {
    if (offset_times == 0) return;

    if (offset_times > 0) { // 右シフト (x7方向へ詰める)
        for (uint8_t y = 0; y < 8; y++) {
            led_matrix_buffer.bytes[y] 
            = led_matrix_buffer.bytes[y] << offset_times;
        }
    } else { // 左シフト (x0方向へ詰める)
        int abs_offset_times = -offset_times;
        for (uint8_t y = 0; y < 8; y++) {
            led_matrix_buffer.bytes[y] 
            = led_matrix_buffer.bytes[y] >> abs_offset_times;
        }
    }
}

/**
 * @brief 画面全体を上下に offset_times ピクセル分シフト（正＝上、負＝下）
 * @param[in] offset_times シフトドット数 (正の値で上シフト、負の値で下シフト)
 */
void led_buffer_shift_rows(int offset_times) {
    if (offset_times == 0) return;

    if (offset_times > 0) { // 上シフト (y=0方向へ移動、下端に0を補填)
        for (int y = 0; y < 8; y++) {
            if (y + offset_times < 8) {
                led_matrix_buffer.bytes[y] 
                = led_matrix_buffer.bytes[y + offset_times];
            } else {
                // y+offset_times=8,9,10の残り下3列は全て0を詰める。
                led_matrix_buffer.bytes[y] = 0x00;
            }
        }
    } else { // 下シフト (y=7方向へ移動、上端に0を補填)
        int abs_offset_times = -offset_times;
        // 引数で受け取った負の値にマイナスを掛けてプラスにして絶対値に変換
        for (int y = 7; y >= 0; y--) {
            // 最大値のy=7から始め、yをデクリメントして行く。
            if (y - abs_offset_times >= 0) {
                led_matrix_buffer.bytes[y] 
                = led_matrix_buffer.bytes[y - abs_offset_times];
            } else {
                led_matrix_buffer.bytes[y] = 0x00;
                // 残った上の列2,1,0は0で埋める。
            }
        }
    }
}


/**
 * @brief 画面バッファ全体を1ピクセル左⇐へ送り出し、右⇒端列に新しい1列データを追加
 * @param[in] new_rightend_col_data 画面右端(7bit目)に追加挿入する縦1列分(8bit ex:0b1111_0000)のデータ
 */
void led_buffer_push_col_from_right(uint8_t new_rightend_col_data) {
    for (uint8_t y = 0; y < 8; y++) {
        uint8_t new_bit = (new_rightend_col_data >> y) & 0x01;
        led_matrix_buffer.bytes[y] 
        = (led_matrix_buffer.bytes[y] >> 1) | (new_bit <<7);
    }
}

/**
 * @brief 画面バッファ全体を1ピクセル右⇒へ送り出し、左⇐端列に新しい1列データを追加
 * @param[in] new_leftend_col_data 左端(0bit目)に追加挿入する縦1列分(8bit ex:0b1111_0000)のデータ
 */
void led_buffer_push_col_from_left(uint8_t new_leftend_col_data) {
    for (uint8_t y = 0; y < 8; y++) {
        uint8_t new_bit = (new_leftend_col_data >> y) & 0x01;
        led_matrix_buffer.bytes[y] 
        = (led_matrix_buffer.bytes[y] << 1) | new_bit;
    }
}

// -------------------------------------------------------------
//  ４．演出・重ね合わせ（合成）バッファ操作
// -------------------------------------------------------------
void led_buffer_draw_pattern(const uint8_t pattern[8]);
// ：指定した8×8ドット絵パターンを一括転写
void led_buffer_overlay_pattern(const uint8_t pattern[8], uint8_t mode);
// ：OR合成 / AND合成 / XOR合成でパターンを重ね合わせ（キャラと背景の衝突や重なり表現に便利）
void led_buffer_rotate_90(void); //画面全体を90度時計回りに回転 

/**
 * @brief 指定した8x8ドット絵パターン(8バイト配列)を一括転写
 * @param[in] pattern 8バイトのパターン配列
 */
void led_buffer_draw_pattern(const uint8_t pattern[8]) {
    if (pattern == NULL) return;
    for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = pattern[i];
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
                led_matrix_buffer.bytes[i] |= pattern[i];
                break;
            case OVERLAY_MODE_AND:
                led_matrix_buffer.bytes[i] &= pattern[i];
                break;
            case OVERLAY_MODE_XOR:
                led_matrix_buffer.bytes[i] ^= pattern[i];
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
    LEDBuffer_t temp = { .bytes = {0} };

    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            // (x, y) ピクセルを取得し、回転先 (7-y, x) へ設定
            bool bit = (led_matrix_buffer.bytes[y] >> x) & 0x01;
            if (bit) {
                temp.bytes[x] |= (1 << (7 - y));
            }
        }
    }
    led_matrix_buffer = temp;
}

// -------------------------------------------------------------
// ５．点灯・タイミング制御などレンダラー（描画）関数群
// ※ バッファの内容を500Hzのダイナミック点灯で物理LEDへ出力表示
// -------------------------------------------------------------

void led_render_blink(uint32_t interval_ms, uint8_t times);
void led_render_scan_once(void); 
void led_render_frame(uint32_t duration_ms); 
void led_render_scroll_text(const char *text, uint32_t step_delay_ms);
void mode_ticker_board(const char *text);

/**
 * @brief 現在のバッファ状態を指定ミリ秒間だけ静止・連続点灯表示する関数
 *（最小単位：8行分を1周だけ走査してダイナミック点灯スキャンする）
 * つまり「1画面描画（ダイナミック点灯）」の基本関数
 * @note  1回の呼び出しで 250μs × 8行 ＝ 約2ms かかる（500Hzスキャン）
 */
void led_render_scan_once(void) {
    for (uint8_t row = 0; row < 8; row++) {
        uint8_t line_data = led_matrix_buffer.bytes[row];
        hw_matrix_output_line(row, line_data);
        // 1行あたりの点灯保持時間（2000μs/8行 = 250us）
        sleep_us(ROW_SCAN_TIME_US);
        // 次の行へ移る直前にブランキング（全消灯）
        hw_matrix_led_blanking();
    }
}

/**
 * @brief 現在のバッファ状態を指定時間の（duration_ms）間だけ静止・連続点灯表示する関数。
 * （現在のバッファ状態をそのまま点灯する）
 * @param[in] duration_ms 点灯保持時間（ミリ秒）。0指定時はデフォルト10000ms(10秒)
 * @note 桁あふれ（オーバーフロー）しないように、uint32_t型を渡します。
 */
void led_render_frame(uint32_t duration_ms) {
    if (duration_ms == 0) duration_ms = DEFAULT_RENDER_DURATION_MS;
    absolute_time_t end_time = make_timeout_time_ms(duration_ms);
    while (!time_reached(end_time)) {
        led_render_scan_once();
    }
}

/**
 * @brief 指定した間隔・回数で画面バッファの内容を点滅させる関数
 * @param[in] interval_ms 点滅間隔（ミリ秒）。0指定時はデフォルト(300ms)
 * @param[in] times 点滅回数。0指定時はデフォルト(10回)
 */
void led_render_blink(uint32_t interval_ms, uint8_t times) {
    // 0 が指定された場合はデフォルト値（定数）を採用する
    if (interval_ms == 0) interval_ms = DEFAULT_BLINK_INTERVAL_MS;
    if (times == 0)       times = DEFAULT_BLINK_TIMES;
    // 現在のバッファ状態を一時退避するための退避用バッファ
    LEDBuffer_t backup = led_matrix_buffer;
    for (uint8_t i = 0; i < times; i++) {
        // 消灯状態を表示
        led_buffer_clear_all();
        led_render_frame(interval_ms);
        // 元の絵を復元して表示
        led_matrix_buffer = backup;
        led_render_frame(interval_ms);
    }
}

/**
 * @brief 指定した文字列を1ドットずつスムーズに左スクロール表示する関数
 * @param[in] text スクロール表示させる文字列(ASCII)
 * @param[in] step_delay_ms 1ピクセル移動ごとの静止保持時間 (ミリ秒)
 */
void led_render_scroll_text(const char *text, uint32_t step_delay_ms) {
    if (text == NULL) return;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        // フォントデータ(8バイト)の取得
        const uint8_t *font_data = get_font_data_ascii(text[i]);
        if (font_data == NULL) continue;
        int maxwidth = ((unsigned char)text[i] < 0x80) ? 5 : 8;
        // 'A'の時 u0041
        // 1文字(5列)分、縦1列ずつ取り出してバッファに挿入＆レンダリング
        for (int col = 0; col < maxwidth; col++) {
            uint8_t col_pattern = 0;
            // ↓文字フォントから1列分(縦方向8ビット)をループで各行から取り出す。
            for (int row = 0; row < 8; row++) {
                // (7 - col) でフォントの左端(bit7)から抽出
                uint8_t bit = (font_data[row] >> (7 - col)) & 0x01;
                col_pattern |= (bit << row);
            }
            // バッファを1ピクセル左シフトし、新しい1列を追加
            led_buffer_push_col_from_right(col_pattern);
            // 指定時間ぶんダイナミック点灯描画
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
 * @brief 1マスずつ順番に打点していくアニメーションテスト(累積)
 */
void test_accumulate_dots_animation(void) {
    led_buffer_clear_all();
    // バファクリアはループ前に一度だけ。
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            led_buffer_set_pixel(x, y, ON);
            led_render_frame(50); // 50msごとに1ドット増える
        }
    }
}

/**
 * @brief 単一1ドット掃引テスト（前のドットは消えて1つのドットが走る）
 */
void test_one_by_one_dot_animation(void) {
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            led_buffer_clear_all();
            // ループの度にバファをクリア
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
 * @brief 全単体テストの一括実行関数
 */
void run_all_unit_tests(void) {
    test_pixel_control();
    test_accumulate_dots_animation();
    test_one_by_one_dot_animation();
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
