/**
 * @file  gyro.h
 * @brief ジャイロ L3GD20 の、Z軸（旋回）の角速度を読みます。
 *
 * ICとの通信は、先生のドライバ（l3gd20/）を使います。このファイルの関数は、そのドライバを、
 * 基礎プログラムの使い方（起動時に1回設定、1msの割り込みからZ軸を読む）に合わせて呼ぶだけです。
 *
 * ジャイロは、返事があれば、毎回（1msごと）読んで、記録に入れます。
 * 走行の制御（旋回の補正）に使うかどうかは、params.h の USE_GYRO で決めます。
 * 止まっているときの値（オフセット）を引く計算と、角度の計算は、control.c にあります。
 */
#ifndef GYRO_H
#define GYRO_H

#include <stdbool.h>
#include <stdint.h>

/** ICを設定します（先生の l3gd20_init()：Z軸だけ有効、1秒に95回の更新、±2000deg/s）。
 *  1msの割り込みを始める前に、main から1回だけ呼びます（中で 100ms 待ちます）。
 *  WHO_AM_I が L3GD20（D4）か L3GD20H（D7）なら true を返します。 */
bool gyro_init(void);

/** gyro_init() のときに読んだ WHO_AM_I レジスタの値を返します。L3GD20 は D4、L3GD20H は D7 です。
 *  ICからは読み直しません。SPI1 は1msの割り込みが使っているので、main から同じSPIを使うと、
 *  割り込みの通信とぶつかるためです。 */
uint8_t gyro_who_am_i(void);

/** gyro_init() で、ジャイロから正しい返事があったなら true。false の間、割り込みはジャイロを読みません。 */
bool gyro_is_present(void);

/** Z軸の角速度 [deg/s] を読みます（オフセットは引いていません）。1msの割り込みから呼びます。
 *  左回り（上から見て反時計回り）が正になるように、board.h の GYRO_Z_SIGN を掛けています。 */
float gyro_read_z_dps(void);

#endif /* GYRO_H */
