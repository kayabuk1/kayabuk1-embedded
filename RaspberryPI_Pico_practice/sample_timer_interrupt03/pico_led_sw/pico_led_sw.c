#include <stdio.h>
#include "pico/stdlib.h"

/*LED0を点滅*/
void out_led0(bool on)
{
    gpio_put(12, on);
}
/*LED1を点滅*/
void out_led1(bool on)
{
    gpio_put(11, on);
}
/*LED2を点滅*/
void out_led2(bool on)
{
    gpio_put(10, on);
}
/*ボード上のLEDに一斉アクセス*/
void out_led_byte(unsigned char data)
{
    out_led0(0x01 & data);
    out_led1(0x01 & (data >> 1));
    out_led2(0x01 & (data >> 2));
}
/*SW0状態を取得*/
bool get_tactSW0(void)
{
    return !gpio_get(15);
}
/*SW1状態を取得*/
bool get_tactSW1(void)
{
    return !gpio_get(14);
}
/*SW2状態を取得*/
bool get_tactSW2(void)
{
    return !gpio_get(13);
}
/*ボード上のタクトスイッチに一斉アクセス*/
unsigned char get_tactSW_byte(void)
{
    return get_tactSW2() << 2 | get_tactSW1() << 1 | get_tactSW0();
}