#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"


int main()
{
    stdio_init_all();
    gpio_set_function(18, GPIO_FUNC_PWM);
    gpio_set_function(19, GPIO_FUNC_PWM);

    uint slice_num 
        = pwm_gpio_to_slice_num(18);

    // システムクロック125MHzを 1/125=1MHz にして扱いやすくしてやる。
    // これがPWM波形反転のカウンターの基準の1カウントになる。
    // つまり 1カウント=1us。これを増減させてPWM周波数を変えていく。
    // clkdivの値は最大250程度に出来るとのこと。
    // そうすると 500kHz までPWMの基準周波数が下がる。
    pwm_set_clkdiv(slice_num, 125.f);
    // pwm周波数が 1MHz/(999+1)＝ 1kHzになる。
    pwm_set_wrap(slice_num, 999);
    // コンペマッチするまでのカウントを999+1※0始まり＝1000に設定。
    // duty比を決定。enmu型でチャンネルが定義されている。
    // ラップ値※一度波形が上がって、下がってまた上がるまでの周期
    // を決める値。最大65535に出来る。
    // 500kHz/65535すると＝7.63Hzまで下げることが出来るとのこと。
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 300);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, 500);
    // PWM波形出力
    pwm_set_enabled(slice_num, true);

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
