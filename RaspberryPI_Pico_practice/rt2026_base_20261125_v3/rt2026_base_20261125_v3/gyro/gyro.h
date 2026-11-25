/**
 * @file  gyro.h
 * @brief ジャイロ L3GD20 の、Z軸（旋回）の角速度を読みます。
 *
 * ICとの通信は、ジャイロの担当の方のドライバ（l3gd20h/）を使います。このファイルの3つの関数は、
 * そのドライバを、基礎プログラムの使い方（起動時に1回設定、1msの割り込みからZ軸を読む）に合わせて呼ぶだけです。
 * params.h の USE_GYRO が 0 の間は、走行の制御には使いません（動作確認メニューは、起動時に読んだ
 * WHO_AM_I の値だけを表示します）。
 */
#ifndef GYRO_H
#define GYRO_H

#include <stdbool.h>
#include <stdint.h>

/** ICを設定します（±2000deg/s）。A/D変換ICの初期化（SPI1の設定）の後で呼びます。
 *  WHO_AM_I が L3GD20（D4）か L3GD20H（D7）なら true を返します。 */
bool gyro_init(void);

/** gyro_init() のときに読んだ WHO_AM_I レジスタの値を返します。L3GD20 は D4、L3GD20H は D7 です。
 *  ICからは読み直しません。SPI1 は1msの割り込みが使っているので、main から同じSPIを使うと、
 *  割り込みの通信とぶつかるためです。 */
uint8_t gyro_who_am_i(void);

/** Z軸の角速度 [deg/s] を読みます。1msの割り込みから呼べます。 */
float gyro_read_z_dps(void);

#endif /* GYRO_H */
