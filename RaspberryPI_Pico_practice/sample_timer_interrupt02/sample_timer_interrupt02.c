#include <stdio.h>
#include "pico/stdlib.h"
// #include "hardware/timer.h"
#include "buzzer/buzzer.h"

// 状態の定義（ステートマシーンを作る為の下準備）
typedef enum 
{
    STATE_1 = 1, // = 0; と書かなくてもデフォで０から割当してくれる。
    STATE_2,
    STATE_3,
} state_t;

// 最初の状態をステート1にしておく。
static volatile state_t state = STATE_1;

bool repeating_timer_callback(
    __unused struct repeating_timer *t)
{   
    static int cnt_1ms = 0;
    // static int cnt_1ms = 0; の「= 0」は、最初に1回だけ実行される
    static bool led_state = false;

    cnt_1ms++;
    beepProcess();

    // if(cnt_1ms >= 500) LED点滅は一時コメントアウト。
    // {   
    //     setBeepPattern(BUZZER_PIN, 0x8000, DO_6);
    //     cnt_1ms = 0; //リセット
    //     gpio_put(25, led_state);
    //     led_state ^= 0x1; //ビット反転(xor)1^1=0,1^0=1
    // }

    switch (state)
    // どの case を実行するかを決めているのは state で、cnt_1ms 
    // ではないということです。cnt_1ms を0にしても、state は変わらない。
    {
        case STATE_1: 
            if(cnt_1ms >= 1000){
                state = STATE_2; 
                setBeepPattern(BUZZER_PIN, 0x0001, DO_6);
                cnt_1ms = 0;
            } 
            break;
        case STATE_2: 
            if(cnt_1ms >= 1500){
                state = STATE_3; 
                setBeepPattern(BUZZER_PIN, 0x0001, MI_6);
                cnt_1ms = 0;
            }             
            break;
        case STATE_3: 
            if(cnt_1ms >= 2000){
                state = STATE_1; 
                setBeepPattern(BUZZER_PIN, 0x0001, SO_6);
                cnt_1ms = 0;
            }             
            break;
    }

    return true;
}

int main()
{
    stdio_init_all();

    gpio_set_function(25, GPIO_FUNC_SIO);
    gpio_set_dir(25, GPIO_OUT);

    initBeep();

    struct repeating_timer timer;
    {
        /* data */
    }
    
    add_repeating_timer_ms(
        -1, repeating_timer_callback, NULL, &timer);
        // 一ミリ秒感覚でセンサーログなどを送ることが多い。
        // 第一引数に正の値で、処理後～処理開始までの間隔指定
        // 負の値で、処理開始～次処理開始までの間隔指定

    while (true) {
        printf("state:%d\n", state);
        sleep_ms(100);
    }
}
