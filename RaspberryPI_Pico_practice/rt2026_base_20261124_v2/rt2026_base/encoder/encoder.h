/**
 * @file  encoder.h
 * @brief 左右のエンコーダを読み、進んだ距離と速さを求めます。【仮のドライバ】
 *
 * エンコーダの担当のドライバが届くまでの、仮の物です。
 * A相とB相の数え方は、Pico SDK のサンプル（pico-examples の quadrature_encoder.pio）を
 * そのまま使っています。先生のサンプルも、同じPIOのプログラムを使っています。
 * 担当のドライバに差し替えるときは、このファイルの4つの関数の中身を入れ替えます。
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#define ENC_SPEED_WINDOW_MS 10 /* 速さを求めるときに、何ms前の数と比べるか。
                                  1m/sで1msに約3個しか進まないので、10ms分をまとめて見ます（仕様書 3.2.4） */

/** PIOを設定します。起動時に1回呼びます。 */
void encoder_init(void);

/** エンコーダの数を読み、速さを更新します。1msの割り込みから呼びます。 */
void encoder_update_1ms(void);

/** 起動してからの数（前進で増える）。left が true なら左、false なら右です。 */
int32_t encoder_count(bool left);

/** 車輪の速さ [m/s]（前進が正）。直前の ENC_SPEED_WINDOW_MS の平均です。 */
float encoder_speed_mps(bool left);

/** 機体が進んだ距離 [m]。左右の平均です。 */
float encoder_distance_m(void);

#endif /* ENCODER_H */
