/**
 * @file  gyro.c
 * @brief ジャイロ L3GD20 を読みます。説明は gyro.h にあります。
 *
 * ICとの通信は、ジャイロの担当の方のドライバ（l3gd20h/l3gd20h.c）の関数を呼んでいます。
 */
#include "gyro.h"

#include "board.h"
#include "l3gd20h/l3gd20h.h"
#include "l3gd20h/l3gd20h_registers.h"
#include "pico/stdlib.h"

static uint8_t who_am_i_value = 0; /* gyro_init() のときに読んだ値 */

uint8_t gyro_who_am_i(void)
{
    return who_am_i_value;
}

bool gyro_init(void)
{
    /* CS のピンを出力にして、H（選んでいない状態）にします。
     * SPI1 そのものの設定（速さ、SCK・MOSI・MISO のピン）は、A/D変換ICの初期化が済ませています。
     */
    gpio_init(PIN_GYRO_CS);
    gpio_set_dir(PIN_GYRO_CS, GPIO_OUT);
    gpio_put(PIN_GYRO_CS, 1);

    l3gd20h_init(); /* 担当の方の設定：電源ON、X・Y・Z軸を有効、±2000deg/s */

    who_am_i_value = l3gd20h_read_reg(L3GD20H_WHO_AM_I);
    return who_am_i_value == 0xD4u || who_am_i_value == 0xD7u;
}

float gyro_read_z_dps(void)
{
    /* Z軸の下位と上位の2つのレジスタだけを読みます。
     * l3gd20h_read_gyro() は3軸（6つのレジスタ）を読むので、1msの割り込みの中では、その分の時間がかかります。
     */
    const uint8_t low = l3gd20h_read_reg(L3GD20H_OUT_Z_L);
    const uint8_t high = l3gd20h_read_reg(L3GD20H_OUT_Z_H);
    const int16_t raw = (int16_t)(((uint16_t)high << 8) | low);
    return (float)GYRO_Z_SIGN * l3gd20h_raw_to_dps(raw);
}
