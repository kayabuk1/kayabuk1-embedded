#include <stdio.h>
#include "pico/stdlib.h"
// #include "hardware/timer.h"

bool repeating_timer_callback(
    __unused struct repeating_timer *t)
{   
    static int cntimes = 0;
    static bool led_state = false;

    cntimes++;
    if(cntimes >= 500){
        cntimes = 0; //リセット
        gpio_put(25, led_state);
        led_state ^= 0x1; //ビット反転(xor)1^1=0,1^0=1
    }

    return true;
}

int main()
{
    stdio_init_all();

    gpio_set_function(25, GPIO_FUNC_SIO);
    gpio_set_dir(25, GPIO_OUT);

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
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
