/**
 * @file  line_sensor.h
 * @brief ラインセンサー6個とマーカーセンサー2個を読み、ラインからのずれを求めます。
 *
 * 値の流れ（仕様書 3.1）：
 *   A/D変換の値（白いほど小さい） → 正規化（黒 = 0、白 = 100） → 2値（白か黒か）
 *   → 6個に重み（[5]から順に +5、+3、+1、-1、-3、-5）を掛けて足す → ずれ（-100〜+100）
 * ずれは、機体がラインの右にあるときが正です。
 */
#ifndef LINE_SENSOR_H
#define LINE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

/** A/D変換IC（SPI1）と、LEDのスイッチのピンを設定します。起動時に1回呼びます。 */
void line_sensor_init(void);

/** LEDを点けて8個を読み、正規化、2値化、ずれの計算までを行います。1msの割り込みから呼びます。 */
void line_sensor_sample(void);

uint16_t line_raw(int i);      /* A/D変換の値（i は 0〜7）                   */
uint8_t  line_norm(int i);     /* 正規化した値（黒 = 0、白 = 100）           */
bool     line_is_white(int i); /* 2値にした結果。白なら true                 */
uint8_t  line_bits(void);      /* 8個の2値を1バイトにまとめた値（ビットi = sensor[i]。白が1） */
int      line_white_count(void); /* ラインセンサー6個のうち、白の個数        */
bool     line_is_lost(void);   /* ラインセンサー6個とも、白がほとんど見えていなければ true */
float    line_displacement(void); /* ラインからのずれ（-100〜+100）          */

/* ----- キャリブレーション（白と黒の値を覚える） -----
 * line_cal_begin() → 機体をラインの上で左右に動かしながら line_cal_update() を繰り返す
 * → line_cal_end()。どれも、1msの割り込みから呼びます。
 */
void     line_cal_begin(void);
void     line_cal_update(void);
bool     line_cal_end(void);   /* 8個とも、白と黒の差が十分にあれば true。false のときは、前の値に戻します */
bool     line_cal_done(void);  /* 電源を入れてから、キャリブレーションが1回でも成功していれば true */
uint8_t  line_cal_fail_bits(void); /* 直前のキャリブレーションで、白と黒の差が足りなかったセンサー（ビットi = sensor[i]） */
uint16_t line_cal_min(int i);  /* 覚えている最小の値（白）                   */
uint16_t line_cal_max(int i);  /* 覚えている最大の値（黒）                   */

#endif /* LINE_SENSOR_H */
