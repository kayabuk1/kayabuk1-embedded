/**
 * @file  motor.h
 * @brief モータードライバ TB67H450FNG（左右2個）を動かします。
 *
 * duty は -1.0〜+1.0 で渡します（+1.0 = 前進の向きに100%、-1.0 = 後退の向きに100%）。
 * ピンの出し方は、仕様書 3.6 の表のとおりです。
 *   duty > 0 : IN1 = H、     IN2 = PWM（Hの割合が 100% - duty）
 *   duty = 0 : IN1 = H、     IN2 = H   （ブレーキ）
 *   duty < 0 : IN1 = PWM（Hの割合が 100% - |duty|）、IN2 = H
 */
#ifndef MOTOR_H
#define MOTOR_H

#define MOTOR_PWM_HZ 200000u /* PWMの周波数 [Hz]。仕様書は100kHz以上、参考資料6.1の計算では約202kHz */

/** PWMを設定します。起動時に1回呼びます。呼んだ直後は、惰性（IN1 = L、IN2 = L）です。 */
void motor_init(void);

/** 左右の duty を設定します（-1.0〜+1.0）。範囲の外の値は、-1.0 か +1.0 に丸めます。 */
void motor_set(float duty_l, float duty_r);

/** ブレーキをかけます（IN1 = H、IN2 = H）。motor_set(0, 0) と同じです。 */
void motor_brake(void);

/** 惰性で止めます（IN1 = L、IN2 = L）。ICは1ms後に待機の状態に入ります。 */
void motor_free(void);

#endif /* MOTOR_H */
