#include <stdio.h>
#include "pico/stdlib.h"

// ■LED0を点滅する関数 ※一般的なCではブール型無くunsigneedcharなど
// を使うが、pico環境では用意されているので使う。
// ●LED０を点滅する関数
void out_led0(bool on)
{
    gpio_put(12, on);
}
// ●LED１を点滅する関数
void out_led1(bool on)
{
    gpio_put(11, on);
}
// ●LED２を点滅する関数
void out_led2(bool on)
{
    gpio_put(10, on);
}
// ■ボード上のLEDを一斉アクセスする関数　※ビットを使って点滅
void out_led_byte(unsigned char led_byte_data)
// 関数名には動詞を書くのが一般的
{
    out_led0(led_byte_data & 0x01);
    out_led1((led_byte_data >> 1) & 0x01);
    out_led2((led_byte_data >> 2) & 0x01);
}

// ■スイッチ用の関数　※普段は０押下で１
// スイッチは内部プルアップで普段１で、押下で０になるので、反転させて返す
bool get_tactSW0(void){
    return !gpio_get(15);
}
bool get_tactSW1(void){
    return !gpio_get(14);
}
bool get_tactSW2(void){
    return !gpio_get(13);
}
// ■ボード上ﾀｸﾄｽｲｯﾁの状態を一斉に取得して返す関数
unsigned char get_tactSW_byte(void){
    unsigned char sw_byte_data = 0;
    sw_byte_data |= get_tactSW0() << 0;
    sw_byte_data |= get_tactSW1() << 1;
    sw_byte_data |= get_tactSW2() << 2;
    return sw_byte_data;
    // return (get_tactSW0() << 0) | (get_tactSW1() << 1) | (get_tactSW2() << 2);
    // ↑の様に一行で書くことも出来る。
    // return ((get_tactSW0()&0x01) << 0) | ((get_tactSW1()&0x01) << 1) | (get_tactSW2()&0x01) << 2);
    // ほかのビットにいらないビットが入っている時はマスクを掛けてから使うと良い。
    // 今回のPICOのSDKは良くできていて、
    // gpio_get()の返り値は０か１しか返さないので、マスクは不要。
}