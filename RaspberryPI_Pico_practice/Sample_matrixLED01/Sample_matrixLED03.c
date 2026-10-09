#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
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

#define ON true
#define OFF false 

// -------------------------------------------------------------
// タイマー・周波数に関する定数定義 (500Hz 設定)
// -------------------------------------------------------------
// ●1フレーム（8行）にかかる時間 
// 　1秒(1,000,000μs) ÷ 500Hz = 2,000μs ＝ 2ms
// ●1行あたりの切り替え時間（sleep_us の値）
// 　2,000μs ÷ 8行 = 250μs
// 1行あたりの点灯時間（250μs）
#define ROW_SCAN_TIME_US 250

// 1フレーム（8行スキャン）にかかる時間を自動計算（ミリ秒）
// (8行 × 250μs) ÷ 1000 ＝ ちょうど 2ms 
#define FRAME_SCAN_TIME_MS ((8 * ROW_SCAN_TIME_US) / 1000)

// デフォルト時間設定
#define DEFAULT_RENDER_DURATION_MS 10000 // 10秒 (2ms×5000回スキャン)
#define DEFAULT_BLINK_INTERVAL_MS 300   // 300ms (2ms×150回スキャン)

// デフォルト点滅間隔
#define DEFAULT_BLINK_TIMES 10


// 原点 (0, 0)：画面の左上隅。
// ■データ構造（共用体 union の活用）
// union（共用体）を使うと、「8バイトの配列」としてもアクセスでき、
// 同時に「(y, x) 座標の1ビット」としても直感的にアクセスできる
// 構造体を作ることができるとのこと。
// ●8ビット分のビットフィールド構造体（1行分）
typedef struct {
    uint8_t x0 : 1;
    uint8_t x1 : 1;
    uint8_t x2 : 1;
    uint8_t x3 : 1;
    uint8_t x4 : 1;
    uint8_t x5 : 1;
    uint8_t x6 : 1;
    uint8_t x7 : 1;
} RowBits;
// ・uint8_t x0 : 1; のように :（コロン）と数字を書くと、
// 指定したビット数（ここでは1ビット）だけをその変数に割り当てる
// という命令になる。
// ・通常、uint8_t 型の変数を宣言すると1バイト（8ビット）消費するが、
// ビットフィールドを使うと 1バイトの領域を8等分して、それぞれに
//  x0 〜 x7 という名前をつける ことができる。
// ・普通の構造体と同じように .（ドット）でアクセス可能。
// ex：row[y].x0 = 1; // y行x0（0ビット目）に 1（点灯）をセット

//■8x8 LEDフレームバッファ（共用体）
typedef union {
    uint8_t bytes[8]; 
    // SPI送信や行一括操作用の8バイト配列
    RowBits grid[8];
    // grid[y].x0 〜 grid.x7 で1ビットずつ直接アクセス！
} LEDBuffer;
// ●共用体（union）は一つのメモリを色々な切り口で扱う。
// 構造体（struct）: 
    // メンバーごとにメモリが横に並ぶ（bytes[8] と grid[8] 
    // で計 16 バイト消費する）
// 共用体（union）: すべてのメンバーが全く同じメモリ空間（先頭アドレス）
    // を共有して重なる（計 8 バイトしか消費しない）
// なので、RowBits型の配列を作って、grid[8]RowBits型が8個入るという形
// にしたら、(y=2, x=3) の1ピクセルだけを点灯させたい時は
// ・g_led_matrix.grid[2].x3 = 1;と言う形でアクセス出来る。
// ・SPI送信で2行目の8ビットを一括送信したい時（そのまま渡せる）
// spi_write_blocking(SPI_PORT, &g_led_matrix.bytes[2], 1);
// 　　　　　　　　　　　　　　　　↑ここが2行目の8ビットを指定

// ■グローバルまたはモジュール内の基準バッファ
// ●なぜLED制御でバッファ（フレームバッファ）が必要なのか？
// もしバッファがなく、1ピクセル点灯させるたびに直接ハードウェア
// （SPIやLED）へデータを送信しようとすると、画面が一瞬チラついたり、
// 表示が崩れたりしてする。
// そのため、マイコンのメモリ（SRAM）上に 
// 「画面の下書き用キャンバス（LEDBuffer）」 を用意しておき：
// 下書きフェーズ: プログラム上で1ピクセルつけたり、
    // 文字をずらしたりして、バッファ上で次のコマの絵を完成させる。
// 描画フェーズ: 完成したバッファのデータを、ダイナミック点灯
    // （タイマーや繰り返し処理）で一気にLEDへ送り出して表示する。
// このように、「画面のデータを作成する場所」 と
//  「LEDを物理的に光らせる場所」 を分けるための「下書き用メモリ領域」
// だから バッファ と呼ばれている。
static LEDBuffer g_led_matrix = { .bytes = {0} };
// 指示付き初期化子（Designated Initializer）」 という文法。
// 共用体は複数のメンバーを持っているので、普通に {0} と書くだけだと
// 「どのメンバーを初期化しているのか」が分からない。
// .bytes = {0} と書くことで、共用体の中の .bytes 配列メンバーを
// 指定して、全要素を 0（全消灯状態）で初期化の意味になる。


// -------------------------------------------------------------
// 表示させたい文字列を入れる配列
// -------------------------------------------------------------
// 表示させたいメッセージ（日本語・英数字ミックスOK）
// C言語コンパイラに「文字」は「数値（文字コード）」に変換されるので、
// そのまま文字列を入れる。
// ■ ↓ UTF-8使う場合は ASCII版をコメントアウトしてこちらを使用
// static const wchar_t scroll_text[] = L" Hello Pico! こんにちは！";
// ■ ↓ ASCIIコード版
static const char scroll_text[] = " HELLO PICO! 12345 ";
// ●「wchar_t(ワイド文字型)」日本語や各種言語の文字(Unicode)を、
// C言語で普通のchar型の様に一文字ずつ扱える様する為の「大きな文字型」。
// 日本語も英数字も、１文字＝１要素として均一に扱える様になる。
// 文字列の先頭の L は、L プレフィックス。コンパイラにwchar_t型で扱うことを
// 伝えるサイン。
// ●const ：変数やポインタの値を「読み取り専用（変更不可）」にする修飾子。



// -------------------------------------------------------------
// ビット反転関数
// -------------------------------------------------------------
static inline uint8_t reverse_bits(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4; // 4ビット単位（前半と後半）を入れ替え
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2; // 2ビットペアを入れ替え
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1; // 隣り合う1ビット同士を入れ替え
    return b;
}

// =============================================================
// 0. ハードウェア直接制御層 (HAL / 内部ヘルパー関数)
//    ※ GPIOやSPIを直接叩いてIC(74HC138/595)を物理操作する低レイヤー
// =============================================================
static inline void hw_matrix_select_active_row(uint8_t row);       
    // 旧: select_active_row
static void        hw_matrix_output_line(uint8_t row, uint8_t line_data); 
    // 旧: display_line

// -------------------------------------------------------------
// 74HC138 (アノード行＝点灯行選択) 設定関数
// -------------------------------------------------------------
static inline void hw_matrix_select_active_row(uint8_t row)
// ●「static」静的修飾子：この関数を「この」C言語ファイル限定で使うという宣言。
// これによって、他Cファイルとの名前衝突エラーを防ぐ。アクセス範囲（スコープ）の制限。
// 変数にstaticを付ける場合と、関数にstaticを付ける場合とで意味が異なる。
// 変数の場合は、メモリ領域（静的データ領域）への永久保持。
// ●「inline」インライン指定子：関数を呼び出す時に、関数への「ジャンプ」を
// 行わずに、コンパイル時に呼び出し元の関数実装内容を直接埋め込み展開するように
// ｺﾝﾊﾟｲﾗに指示する命令。
// 通常の関数の呼び出しはスタックへの退避などのわずかなオーバーヘッドが発生する。
// LEDのダイナミック点灯の様に、１秒間に何千回も高速で呼び出す関数は、inline化して
// ジャンプするオーバーヘッドをなくして、メインループで直接内容を実行出来る様に
// する。※その分メインループの使用メモリ量が増える。
// ●static inline とセットで書くのか？
// inline だけだと、コンパイラによっては「外部のファイルから呼ばれるかもしれ
// ないから、一応関数の実体も残しておこう」と判断し無駄なコードが残ってしまうことがある。
// static（このファイル限定）と inline（埋め込み）をセットで書くことで、
// コンパイラに「他ファイルから絶対呼ばれないから、完全に埋め込んで関数の
// 実体は消し去ってOK！」と伝えることができ、無駄無くなる。
// ●「uint8_t row」<stdint.h>で定義されている「符号無し8ビット整数(0～255)」。
// unsigned char型と同じサイズだが、8bit(1byte)サイズであることを「明示」する
// 為にこちらを使う。「row」は点灯させたい行を受け取る変数。
{
    gpio_put(MUX_A, (row >> 0) & 0x01); // 0ビット目
    gpio_put(MUX_B, (row >> 1) & 0x01); // 1ビット目
    gpio_put(MUX_C, (row >> 2) & 0x01); // 2ビット目
    // ●74HC138との連携：74HC138は、3本のバイナリ入力端子(A,B,C)の「H/L」の
    // 組み合わせで8本の出力端子(Y0~Y7)のどれか「１つ」に「Low」を出力する、
    // 3to8ラインデコーダ機能を持ったIC。マルチプレクサの1種。
    // ●MUX_B（1ビット目＝2の位）の出力例
    // row >> 1: 右に 1 ビットシフト（右に1つずらす）。
    // 例：3 (2進数 011) を右に1つずらすと 001 になり、
    // 欲しい「1ビット目」が一番右端へ移動 して来る。
    // ●& 0x01: ビットAND演算子：0x01（2進数で 00000001）と論理積をとることで、
    // 一番右（0ビット目）以外の余計な桁を全すべて0でマスク（消去） して、
    // 0 または 1 だけを取り出し、MUX_B()に1ビット目(2の位)のH/Lが取り込まれる。
}

// -------------------------------------------------------------
// 1行分の描画関数
// -------------------------------------------------------------
void hw_matrix_output_line(uint8_t row, uint8_t line_data)
{   
    // ●uint8_t row: 表示対象の行番号（0 〜 7）。
    // uint8_t line_data: その行に表示させたい 1行分（8ドット）のLED点灯パターン。
    // 元データでは、1 ＝ 点灯（ON）、0 ＝ 消灯（OFF）という直感的なデータになっています。

    // ●カソード制御のためビット反転(~)
    // 74HC595はLEDカソード側を制御している＝LEDを点灯させるには、
    // カソード側をLowにする必要がる。その為、1=点灯になっている元データを
    // ビット反転演算子で全ビットを反転させる。
    uint8_t inv_data = ~line_data;
    spi_write_blocking(SPI_PORT, &inv_data, 1);
    // ●int spi_write_blocking
        // (spi_inst_t *spi, const uint8_t *src, size_t len)
    // ●spi_inst_t: Pico SDKで定義されている「SPIペリフェラル（ハードウェア機能）
    // を管理する構造体」 の型名。RP2040マイコン内部のSPI回路のレジスタ群が
    // まとめられている。
    // * (ポインタ): 構造体の大きな実体をまるごとコピーして渡すのではなく、
    // 「その構造体が置いてあるメモリのアドレス」 を渡す。
    // 「Pico内に2つあるSPI回路（spi0 または spi1）のどちらを使って送信するか？」
    // を指定する。コードで SPI_PORT（spi0）を渡している。プリプロセス部でdefine
    // されているspi0自体も spi_inst_t型へのポインタ型として定義されている。
    // ● const uint8_t *src
    // ● *src (Source / ポインタ): 送信したいデータが格納されている 
    // 「メモリの先頭番地（ポインタ）」
    // &inv_data: アドレス演算子 & 。
    // 送信したい変数 inv_data が入っている「メモリのアドレス（ポインタ）」を
    // 関数に渡す。&inv_data と &（アドレス演算子）を付けて渡した理由は、
    // この引数が「値そのもの」ではなく「アドレス（ポインタ）」を渡す定義になっているから。
    //  ●blocking（ブロッキング）:
    // 「送信が完了するまで、CPUの処理をここで一時停止して待つ（ブロックする）」
    // という意味です。送信完了を確認してから次の処理へ進むため、
    // 安全にデータが送られる。
    // ● size_t len （送信バイト数）
    // size_t: <stddef.h> や <stdio.h> などで定義されている
    // 「メモリのサイズや要素数を表すための標準的な符号なし整数型」。
    // 環境に応じて最適なサイズが定義されており、RP2040（32ビットマイコン）では
    //  uint32_t と同じ 32ビット幅になる。
    // 64ビットのPC環境（WindowsやLinuxなど）では、size_t は uint64_t（64ビット幅）。
    // ●len (Length): src のアドレスから「何バイト分を順番に送るか」
    // というデータ長指定。
    // 今回は 1 バイト（8ビット）送るため 1 を指定。
    // もし 10 バイトの配列をまとめて送りたければ 10 を指定。

    // ラッチパルス(RCK)で出力を更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);
    // ●sleep_us(1) の役割:
    // 1 マイクロ秒だけ待機。ICが「信号が変化した」と確実に認識するための
    // 最低限のパルス幅を確保。

    // 行選択
    hw_matrix_selct_active_row(row);
    // ●行選択（アノード側の切り替え）
    // ●set_mux_row(row);
    // 上部に実装してある、アノード行選択用set_mux_row() 関数を呼び出し、
    // 74HC138（アノード側）へ指定した row（行番号）の信号を送る。
    // 指定した行の PNP トランジスタが ON になり、
    // その行のアノードに 5V が供給される。
    // これにより、「今 5V が通電したアノード行」と「74HC595で 0V (LOW) に
    // 引き下げられたカソード列」の交差点にあるLEDだけが点灯する。
}

// -------------------------------------------------------------
// １．バッファ操作（ゲッター・セッター、全画操作など）モジュール
// 　　※下書きキャンバス(g_led_matrix)」のみを変更するメモリ操作
// -------------------------------------------------------------
/** 
// 1ピクセル（ビット）の点灯(1)/消灯(0)を設定
*/
void led_buffer_set_pixel(uint8_t x, uint8_t y, bool on_off){
    // 範囲外ガード（0〜7以外の値が入ってきたら何もしない）
    if (y >= 8 || x >= 8) return;
    // ビットフィールドメンバには配列に
    // x の値（0〜7）に応じて、対応するビットフィールドメンバに直接代入
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
// ：指定座標の1ピクセルの状態を取得
*/
bool led_buffer_get_pixel(uint8_t x, uint8_t y){
    if (y >= 8 || x >= 8) return false;
    // 戻り値の型が boolのため、早期リターンも何かboolを返す必要がある。
    switch (x) {
        case 0: return g_led_matrix.grid[y].x0;
        case 1: return g_led_matrix.grid[y].x1;
        case 2: return g_led_matrix.grid[y].x2;
        case 3: return g_led_matrix.grid[y].x3;
        case 4: return g_led_matrix.grid[y].x4;
        case 5: return g_led_matrix.grid[y].x5;
        case 6: return g_led_matrix.grid[y].x6;
        case 7: return g_led_matrix.grid[y].x7;
        // stdbool.h の設定で関数の戻り値型が bool の場合、
        // 0 は false、1（非ゼロ）は true に自動的に変換される。
        // x0（1ビット）しか入っていないので自動的にtrue/falseになる。
    }
}


/** 
// 全画面消灯（全ビット0）
*/
void led_buffer_clear_all(void){
     for (int i = 0; i < 8; i++) {
        g_led_matrix.bytes[i] = 0x00; //0b0000 0000
        // 共有体の8ビットごと扱えるbytes[]でアクセスして全部0に。
    }
}

/** 
// 全画面点灯（全ビット1）
*/
void led_buffer_fill_all(void){
     for (int i = 0; i < 8; i++) {
        g_led_matrix.bytes[i] = 0xFF; // 0b1111 1111
    }
}

/** 
// 全画面の白黒（点灯/消灯）反転
*/
void led_buffer_invert_all(void){
     for (int i = 0; i < 8; i++) {
        g_led_matrix.bytes[i] = ~g_led_matrix.bytes[i]; 
        // 全ビット反転
    }
}

// -------------------------------------------------------------
// ２．行・列（ライン）単位のバッファ操作
// -------------------------------------------------------------
// void led_buffer_set_row(uint8_t y, uint8_t row_data)：指定した1行に8ビットデータを書き込み
// uint8_t led_buffer_get_row(uint8_t y)：指定した1行の8ビットデータを取得
// void led_buffer_set_col(uint8_t x, uint8_t col_data)：指定した1列に8ビットデータを縦方向に書き込み
// uint8_t led_buffer_get_col(uint8_t x)：指定した1列の8ビットデータを取得

// -------------------------------------------------------------
// ３．シフト（移動）バッファ操作
// -------------------------------------------------------------
// void led_buffer_shift_rows(int offset)：画面全体を上下に offset ピクセル分シフト（正＝上、負＝下）
// void led_buffer_shift_cols(int offset)：画面全体を左右に offset ピクセル分シフト（正＝左、負＝右）

// -------------------------------------------------------------
//  ４．演出・重ね合わせ（合成）バッファ操作
// -------------------------------------------------------------
// void led_buffer_draw_pattern(const uint8_t pattern[8])
// ：指定した8×8ドット絵パターンを一括転写
// void led_buffer_overlay_pattern(const uint8_t pattern[8], uint8_t mode)
// ：OR合成 / AND合成 / XOR合成でパターンを重ね合わせ（キャラと背景の衝突や重なり表現に便利！）
// void led_buffer_rotate_90(void)：画面全体を90度時計回りに回転 ★追加案

// -------------------------------------------------------------
// ５．点灯・タイミング制御などレンダラー（描画）関数群
// ※ バッファの内容を500Hzのダイナミック点灯で物理LEDへ出力表示する
// -------------------------------------------------------------
// void led_render_blink(uint32_t interval_ms, uint8_t times)
// :指定した間隔・回数で画面を点滅させる
// void led_render_scan_once(void) //（1周スキャン関数）
// 役割: 0行目〜7行目を1周だけダイナミック点灯させる最小単位の関数。
// 1行につき 2ms × 8行 ＝ 約16ms（約1/60秒＝1コマ分）で処理が終わります。
// void led_render_frame(uint32_t duration_ms) //（フレーム表示・静止保持関数）
// 役割: led_scan_once() を指定された時間（ミリ秒）ぶんだけ
// 何度も繰り返し呼び出して、画面の静止画を保持して描画する関数。


// ① 最小単位：8行分を1周だけダイナミック点灯スキャンする関数
// =「1画面描画（ダイナミック点灯）」基本関数
// （1回の呼び出しで 2ms × 8行 ＝ 約16ms かかる）
void led_render_scan_once(void) {
    for (uint8_t row = 0; row < 8; row++) {
        // 共用体バッファから最新の y=row 行目の8ビットデータ
        // （0b...）を直接参照
        uint8_t line_data = g_led_matrix.bytes[row];
        // 既存の1行描画関数を呼び出す（7 - row で上下反転も吸収）
        hw_matrix_output_line(7 - row, line_data);
        // 1行あたりの点灯保持時間（2000μs = 2ms）
        sleep_us(ROW_SCAN_TIME_US);
    }
}

/**
* ② 指定した時間（duration_ms）の間、
* 現在のバッファ状態を点灯表示する関数
* ※1画面描画スキャン関数led_scan_onecを繰り返し呼んで
* マトリクスLED全体の描画を指定時間維持する関数
*/
void led_render_frame(uint32_t duration_ms) {
    if (duration_ms == 0) duration_ms = DEFAULT_RENDER_DURATION_MS;
    // 1回の scan_once に約16msかかるため、
    // 経過時間を計算しながらループする
    uint32_t elapsed_ms = 0;
    // 🔹 render（レンダリング / 描画）とは？
    // コンピュータグラフィックやマイコン制御における render は、
    // 「メモリ上のデータ（バッファ）をもとに、実際に画面やLEDへ出力・
    // 描画する処理」 のことです。 つまり led_render... は
    // 「バッファに描いた下書きを実際のLEDへ映し出す関数」という
    // 意味になります。
    // 🔹 frame（フレーム）とは？
    // パラパラマンガや動画の 「1コマの静止画」 のことです。
    // LEDマトリクス制御では、「ある一瞬に8×8のLED全体で
    // 表示されている1枚の点灯パターン」 を1フレームと呼びます。
    // 🔹 なぜ duration_ms（表示時間）は uint32_t なのか？
    // duration ＝ 持続時間 / 表示する時間
    // ms ＝ ミリ秒（1/1000秒）
    // ミリ秒単位で「1秒（1000ms）」や「10秒（10000ms）」、
    // あるいは「1分（60000ms）」といった大きめの時間を指定したときに
    // 桁あふれ（オーバーフロー）しないようにするためです。
    // uint8_t（最大255＝約0.25秒までしか測れない）では不十分なため
    // 時間を扱う引数には uint32_t（最大約49日分までOK）を使うのが
    // 標準的なルールです。
    
    // 指定された時間（duration_ms）に達するまでスキャンを繰り返す
    while (elapsed_ms < duration_ms) {
        led_render_scan_once();
        elapsed_ms += FRAME_SCAN_TIME_MS; // 1周で約16ms経過
    }
}

/**
 * 指定した間隔・回数で画面を点滅させる関数
 */
void led_render_blink(uint32_t interval_ms, uint8_t times) {

    // 0 が指定された場合はデフォルト値（定数）を採用する
    if (interval_ms == 0) interval_ms = DEFAULT_BLINK_INTERVAL_MS;
    if (times == 0)       times = DEFAULT_BLINK_TIMES;
    // 現在のバッファ状態を一時退避するための退避用バッファ
    LEDBuffer backup = g_led_matrix;

    for (uint8_t i = 0; i < times; i++) {
        // 消灯状態を表示
        led_buffer_clear_all();
        led_render_frame(interval_ms);

        // 元の絵を復元して表示
        g_led_matrix = backup;
        led_render_frame(interval_ms);
    }
}

/**
 * 1ドットずつ順番に点灯していくテストアニメーション関数
 */
void led_lender_test_one_by_one(void){
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            // 内部で switch(x) が働いて
            // .x0 〜 .x7 へアクセスして(x,y)LEDに１をセット
            led_buffer_set_pixel(x, y, ON);
            // 2. そのバッファ状態を 100ms 間ダイナミック点灯表示する！
            led_render_frame(100);
        }
    }
}

// 4. 最初の実装ステップ（提案）
// いきなり全部作るのではなく、一番基礎となる「1ビット点灯・消灯」から順番に積み上げていくのが一番確実に動作確認できます！
// ステップ1: LEDBuffer 型の定義と、led_set_pixel(y, x, state) / led_get_pixel(y, x) の実装
// ステップ2: led_clear_all() と led_set_row() の実装
// ステップ3: これらを使った「1ピクセルずつ斜めに点灯していくアニメーション」を書いて動作テスト
// ステップ4: シフト関数やパターン描画、点滅関数の追加
// この設計・構成についてどう思われますか？ まずは「ステップ1：データ構造の定義と1ビット点灯・消灯関数（ゲッター/セッター）」のコード作成から始めてみますか？














/* 一時コメントアウト

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
    // ●uint8_t display_buffer[8] = {0};
    // 8×8マトリクスLEDに「今画面に映っているドット絵」情報を保持する
    //  8バイトのメモリ領域。（ここに入れた8列分のビットデータが点灯される）
    // 初期状態: {0} と書くことで、8行すべて 0x00（全消灯＝黒画面）からスタート。
    // ※74HC595はLow点灯だが、display_line関数でビット反転させるのでこれでOK。
    // ●[] = {0};
    // 配列を {} で初期化する際、指定した要素数が配列のサイズより少ない場合
    // 「書かれていない残りの要素は、すべて自動的に 0（または NULL）で初期化
    // しなければならない。

    // ■ASCII文字入力を使う場合は通常の strlen を使用する上行をONに
    size_t text_length = strlen(scroll_text);
    // size_t text_length = wcslen(scroll_text);
    // ●wcslen() 関数：
    // ワイド文字列の長さをワイド文字単位（文字数）で計算するC/C++の標準
    // ライブラリ関数。ここで20行目で指定した表示させたい、ワイド文字列の
    // 正確な文字数（要素数）を取得して size_t型の変数に保存。

    while (true)
    // ●一層目：電源が切れるまでメッセージを最初から繰り返す。
    {
        // 1文字ずつ順番に取り出す
        for (size_t char_idx = 0; char_idx < text_length; char_idx++)
        // 表示するメッセージから、0番目の文字（' '）、1番目の文字（'H'）、
        // 2番目の文字（'e'）…と、(文字列の長さ-1)まで1文字ずつ順番に
        // 処理対象を切り替える。
        {   
            // 【修正後】ASCII対応関数を呼び出す
            const uint8_t *next_font 
            = get_font_data_ascii(scroll_text[char_idx]);
            // ■UTF-8版を使う場合は↑コードコメントアウトして↓コードを逆にコメント外す。
            // 文字コードからフォントデータ（8バイト）を取得
            // const uint8_t *next_font 
            // = get_font_data(scroll_text[char_idx]);
            // ●get_font_data(...): 
            // 取り出した1文字（例: L'H'）を渡し、misaki_font.h の検索テーブルから、
            // その文字の8バイトLEDフォントデータへの先頭アドレス
            // （const uint8_t* ポインタ）を受け取る。
            // ※return font_table[i].data;で返ってくる。※dataは配列名=&data[0]

            // 1文字（8列）分、1ドットずつ左へシフト
            for (int col = 0; col < 8; col++) //column=列※縦方向
            {
                // 各行（0〜7行）のビットを左へ1ピクセル移動し、
                // 新しいドットを右端に追加
                for (int row = 0; row < 8; row++) //row=行※横方向
                {
                    uint8_t new_bit = (next_font[row] >> (7 - col)) & 0x01;
                    // ●① 1ピクセルを抽出する式
                    // ●next_font[row]: 
                    // 取得したフォントデータの row 行目（1バイト＝8ビット）。
                    // ●(7 - col): 
                    // フォントデータは 7ビット目が一番左のドット、
                    // 0ビット目が一番右のドット に対応している。
                    //  { L'\u0048', { 0xA0, 0xA0, 0xA0, 0xE0, 0xA0, 0xA0, 0xA0, 0x00 } },'H'
                    // next_font[n]=fonta_table.data[0]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[1]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[2]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[3]=0xA0=0b11100000
                    // next_font[n]=fonta_table.data[4]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[5]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[6]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[7]=0xA0=0b00000000
                    // ◆画面バッファの初期状態 display_buffer[3] =
                    //  0b00000000（全消灯）の時の動き。
                    // ●col = 0（最初の列）のとき ➔ 7 - 0 = 7
                    // （一番左のビットを抽出）
                    //●抽出式: new_bit = (0b1110 0000 >> (7-0))&0x01
                    // なので右へ7ビットシフト＝0b0000 0001➔new_bit = 1
                    //●結合式: display_buffer[3] =
                        //  (display_buffer[3] << 1) | new_bit
                    // もとの画面: 0b00000000
                    // << 1（左へ1ピクセルずらす） ➔ 0b00000000
                    //  | 1（論理和の結果、右端に 1が来る ） ➔ 0b00000001
                    // ➔ 画面の状態: ○ ○ ○ ○ ○ ○ ○ ● （右端にHの一番左の線が現れる）
                    // ●col = 7（最後の列）のとき ➔ 7 - 7 = 0（一番右のビットを抽出）
                    // >>（右シフト）と & 0x01（マスク演算）: 抽出したいビットを
                    // 右端（0ビット目）まで右シフトでずらし、0x01（00000001）と論理積（AND）を
                    // とることで、他の桁を消去して純粋な 1（点灯）か 0（消灯）だけを取り出す。
                    // この1ビットずらす作業を1～8行すべてに行う。

                    display_buffer[row] =
                     (display_buffer[row] << 1) | new_bit;
                    // ●display_buffer[row] << 1
                    // ➔画面全体をベルトコンベアのように1ピクセル左へ送り出している。
                    // ●| new_bit
                    // ➔押し出されて空いた右端の1ピクセル枠に、フォントからくり抜いた
                    // 次のドット（1か0）をパズルハメしている。
                    // ● (display_buffer[row] << 1): 
                    // 現在画面に映っている行データを 左に1ビット（1ピクセル）左シフト。
                    // これにより、画面全体のドットが左へ1ピクセル押し出され、
                    // 一番右端（0ビット目）が 0（空き）になる。
                    // | new_bit: 論理和（OR）演算子 を使い、空いた右端の0ビット目に、
                    // 先ほど抽出した新しい1ピクセル new_bit を追加する。
                }

                // 1ドットシフトした状態でダイナミック点灯（表示スピード調整）
                for (int frame = 0; frame < 18; frame++)
                {
                    for (uint8_t row = 0; row < 8; row++)
                    {   
                        // 【修正後】上下（7 - row）を渡す。
                        display_line(7 - row, display_buffer[row]);

                        // display_line(row, display_buffer[row]);
                        // ビット操作し作った表示行の点灯パターンを
                        //（表示行を選択＆その列の点灯パターンを）渡して点灯。
                        sleep_us(2000); // 2ms待機
                    }
                    // なにをしているのか？: 
                    // 1ピクセル左へ動かした状態の画面バッファ（display_buffer）
                    // を、人間の目で見える時間だけ静止して点灯保持
                    // （表示スピードの制御）。
                    // スピード決定の時間の計算display_line(...) ＋
                    //  sleep_us(2000): 1行分を描画して 2000μs（2ミリ秒）待機。
                    // この frame < 6 の 6 や sleep_us(2000) の数値を変更する
                    // ことで、スクロールのスピードを速くしたり遅くしたり出来る。
                }
            }
        }
    }
}
// 文字方向についてチェック。
//{ L'\u0041', { 0x40, 0xA0, 0xA0, 0xA0, 0xE0, 0xA0, 0xA0, 0x00 } }, // 'A'
// 0b0100 0000
// 0b1010 0000
// 0b1010 0000
// 0b1010 0000
// 0b1110 0000
// 0b1010 0000
// 0b1010 0000
// 0b0000 0000
// { L'\u3093', { 0x10, 0x10, 0x20, 0x30, 0x4A, 0x4A, 0x84, 0x00 } }, // 'ん'
// 0b0001 0000
// 0b0001 0000
// 0b0010 0000
// 0b0011 0000
// 0b0100 1010
// 0b0100 1010
// 0b0000 0000


一時コメントアウト*/

// -------------------------------------------------------------
// 個別テストモジュール関数群
// -------------------------------------------------------------

// テスト1：1ピクセル指定の点灯確認
void test_pixel_control(void) {
    led_buffer_clear_all();
    led_buffer_set_pixel(0, 0, ON); // 左上
    led_buffer_set_pixel(7, 7, ON); // 右下
    led_render_frame(2000);  // 2秒保持
}

// テスト2：行・列単位の操作確認
void test_line_control(void) {
    led_buffer_clear_all();
    // led_set_row(3, 0b11110000); // 3行目の前半4ドット
    led_render_frame(2000);

    led_buffer_clear_all();
    // led_set_col(4, 0b00001111); // 4列目の後半4ドット
    led_render_frame(2000);
}

// テスト3：全点灯＆点滅確認
void test_blink_control(void) {
    led_buffer_fill_all();
    led_render_frame(1000);
    
    // デフォルト値 (0, 0) で点滅テスト！
    led_render_blink(0, 0); 
}

// すべての単体テストを一括実行するまとめ関数
void run_all_unit_tests(void) {
    test_pixel_control();
    test_line_control();
    test_blink_control();
}

int main(void) {
    // 1. ハードウェア初期化
    // init_hardware();

    // 2. 単体テストを一律実行
    run_all_unit_tests();

    // 3. メインループ（テスト終了後の待機や本番処理）
    while (true) {
        // 全点灯のまま静止表示、あるいは次の動作へ
        led_buffer_fill_all();
        led_render_frame(DEFAULT_RENDER_DURATION_MS);
    }
}
