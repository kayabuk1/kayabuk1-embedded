#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "buzzer/buzzer.h"
#include "pico_led_sw/pico_led_sw.h"
#include "dotmatrix_led_8x8/dotmatrix_led_8x8.h"
#include "repeating_timer1ms.h"
#include "quadrature_encoder.pio.h"
#include "font/font8x8_basic.h" //文字フォント

// global
extern const uint sm;
extern PIO pio;
int32_t encoder_value;
// 先生はこう作ったというお手本のサンプルコードが配布された。
// ◆読み取ったこと疑問点などを書いてみるか。
// ●初めて見た用語について調べた内容。
// このファイルは名前から、タイマ割込みハンドラの実体が書いてあるな。
// const は読み取り専用＝定数。externは実体は他のファイルにある。
// ⇒なぜヘッダファイルにextern で書いておかずにここに書いたのだろうか？

// 1msインターバルタイマ割込み関数
bool repeating_timer_callback(__unused struct repeating_timer *t)
{
    static int cnt1ms = 0;
    static uint8_t dotmatrix_led_8x8_buff[8] = {0}; 
    // dotmatrix 表示用バッファ
    static state_t state = STATE_1;                 
    // 状態遷移の状態
    // static は最初に一度だけ実行され、静的領域に値が保持される。

    // 1ms毎インクリメント
    cnt1ms++;
    // beep処理、1ms毎に実行
    beepProcess();
    // エンコーダパルス数取得
    encoder_value = quadrature_encoder_get_count(pio, sm);
    // ●quadranture_encoder_get_count()はPico公式で用意されている、
    // ロータリーエンコーダのカウント値を取得する為の関数。
    // ●PIO（Programmable I/O)CPUを介さず短い命令が実行可能なIO
    // ●sm(State Machine)PIO（Programmable I/O）のステートマシン
    // （SM）は、CPUから独立して高速なGPIO操作や独自プロトコルの
    // 送受信をバックグラウンドで行うための小型の専用プロセッサ。

    // ドットマトリクスLED表示
    gpio_put(HC138_PIN_A, 0);
    gpio_put(HC138_PIN_B, 1);
    gpio_put(HC138_PIN_C, 0);
    hc595_write(0xaa);

    // 状態遷移
    switch (state)
    {
    case STATE_1:
        if (get_tactSW0())
        {
            state = STATE_2; // 次の状態を設定
            setBeepPattern(BUZZER_PIN, 0x8000, DO_6);
            out_led_byte(0x1);
        }
        break;
    case STATE_2:
        if (get_tactSW1())
        {
            state = STATE_3; // 次の状態を設定
            setBeepPattern(BUZZER_PIN, 0xa000, DO_6);
            out_led_byte(0x2);
        }
        break;
    case STATE_3:
        if (get_tactSW2())
        {
            state = STATE_1; // 次の状態を設定
            setBeepPattern(BUZZER_PIN, 0xa800, DO_6);
            out_led_byte(0x3);
        }
        break;
    }
    return true;
}
