/**
 * @file  gyro.c
 * @brief ジャイロ L3GD20 を読みます。説明は gyro.h にあります。
 *
 * ICとの通信は、先生のドライバ（l3gd20/l3gd20.c）の関数を呼んでいます。
 *   l3gd20_init()      : SPI1 とICの設定
 *   l3gd20_read_reg()  : レジスタを1つ読む（WHO_AM_I の確認に使います）
 *   l3gd20_read_raw()  : Z軸の下位と上位の2バイトを、1回の通信で読む
 * 先生の実演のプログラムは、main からジャイロを読んでいました。基礎プログラムでは、同じ SPI1 を
 * 1msの割り込みがA/D変換ICに使うので、ジャイロも割り込みの中で読みます（main から読むと、通信がぶつかります）。
 * そのため、main から呼ぶ形の l3gd20_calibrate() と l3gd20_read_gyro() は、使っていません。
 * オフセットは、control.c が、割り込みの中で測ります。
 */
#include "gyro.h"

#include "board.h"
#include "l3gd20/l3gd20.h"
#include "l3gd20/l3gd20_registers.h"
#include "pico/stdlib.h"

static uint8_t who_am_i_value = 0; /* gyro_init() のときに読んだ値 */
static bool    present = false;    /* 正しい返事があったか         */

uint8_t gyro_who_am_i(void)
{
    return who_am_i_value;
}

bool gyro_is_present(void)
{
    return present;
}

bool gyro_init(void)
{
    l3gd20_init();
    who_am_i_value = l3gd20_read_reg(L3GD20_WHO_AM_I);
    present = (who_am_i_value == 0xD4u || who_am_i_value == 0xD7u);
    return present;
}

float gyro_read_z_dps(void)
{
    /* 生の値 1 が 0.07 deg/s です（±2000deg/s の設定のとき）。例：生の値 1000 → 70 deg/s。 */
    return (float)GYRO_Z_SIGN * (float)l3gd20_read_raw() * L3GD20_SENSITIVITY_2000DPS;
}
