#include <stdio.h>
#include "pico/stdlib.h"
// #include "hardware/timer.h"
#include "buzzer/buzzer.h"
#include "pico_hardware_setup/pico_hardware_setup.h"
#include "pico_led_sw/pico_led_sw.h"
#include "repeating_timer_1ms.h"



// 最初の状態をステート1にしておく。
volatile state_t state = STATE_1;

bool repeating_timer_callback(
    __unused struct repeating_timer *t)
{   
    static int cnt_1ms = 0;
    // static int cnt_1ms = 0; の「= 0」は、最初に1回だけ実行される
    static bool led_state = false;

    cnt_1ms++;
    // beepProcess();

    static uint8_t prev_sw = 0;           // 前回のスイッチの状態

    beepProcess();

    uint8_t sw = get_tactSW_byte();
    uint8_t pressed = sw & ~prev_sw;      
    // 前回0で今回1のビット＝今押された瞬間
    prev_sw = sw;

    if (pressed & 0x01) state = STATE_1;  // SW0が押された瞬間
    if (pressed & 0x02) state = STATE_2;  // SW1が押された瞬間
    if (pressed & 0x04) state = STATE_3;  // SW2が押された瞬間
    
    // if(cnt_1ms >= 500) LED点滅は一時コメントアウト。
    // {   
    //     setBeepPattern(BUZZER_PIN, 0x8000, DO_6);
    //     cnt_1ms = 0; //リセット
    //     gpio_put(25, led_state);
    //     led_state ^= 0x1; //ビット反転(xor)1^1=0,1^0=1
    // }
    if (pressed)                          // 何か押された瞬間だけ鳴らす
    {
        switch (state)
    // どの case を実行するかを決めているのは state で、cnt_1ms 
    // ではないということです。cnt_1ms を0にしても、state は変わらない。
        {
            case STATE_1:
                setBeepPattern(BUZZER_PIN, 0x8001, DO_6);
                out_led_byte(0x01);
                break;
            case STATE_2:
                setBeepPattern(BUZZER_PIN, 0x8001, MI_6);
                out_led_byte(0x02);
                break;
            case STATE_3:
                setBeepPattern(BUZZER_PIN, 0x8001, SO_6);
                out_led_byte(0x04);
                break;
        }
    }
    return true;
}
