#include <stdio.h>
#include "pico/stdlib.h"

/*led0を点滅*/
void out_led0(bool on)
{
    gpio_put(12, on);
}
/*led1を点滅*/
void out_led1(bool on)
{
    gpio_put(11, on);
}
/*led2を点滅*/
void out_led2(bool on)
{
    gpio_put(10, on);
}

/*ボード上のLED点灯*/
void out_led_byte(unsigned char data)
{
    out_led0(0x01 & data);
    out_led1(0x02 & data);
    out_led2(0x04 & data);
}

// SWの状態を読取る関数
bool get_tactSW0(void)
{
    return !gpio_get(15);
    // ボード上のSWは押すとLow、通常時Highなので、
    // 直感的に押されたとき「１」になるように、
    // 「!」で論理反転をする。
}
bool get_tactSW1(void)
{
    return !gpio_get(14);
}
bool get_tactSW2(void)
{
    return !gpio_get(13);
}

/*ボード上のタクトスイッチ状態取り込み*/
unsigned char get_tactSW_byte(void)
{
    return 
    get_tactSW2() << 2 | get_tactSW1() << 1 | get_tactSW0();
    // ON,ON,ONなら、0b0000_0111を返す。
}
