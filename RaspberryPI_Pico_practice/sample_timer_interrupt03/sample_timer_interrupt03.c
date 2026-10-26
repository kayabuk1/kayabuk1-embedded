#include <stdio.h>
#include "pico/stdlib.h"
// #include "hardware/timer.h"
#include "buzzer/buzzer.h"
#include "pico_hardware_setup/pico_hardware_setup.h"
#include "pico_led_sw/pico_led_sw.h"
#include "repeating_timer_1ms.h"

int main()
{   
    hardware_setup();
    // ↑ボード上のスイッチとLEDがセットアップされる。

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
        printf("sw:%02X state:%d\n", get_tactSW_byte(), state);
        sleep_ms(100);
    }
}
