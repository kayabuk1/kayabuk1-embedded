/**
 * @file  control.h
 * @brief 1msの割り込みの中身です。センサーを読み、走行の状態を進め、モーターを動かし、記録します。
 *
 * main との分担：
 *   割り込み（このファイル） : センサー、エンコーダ、スイッチ、ブザー、走行の制御、log_put()
 *   main                     : flash_task()、消去、読み出し、スイッチの操作の受け付け
 * フラッシュメモリ（SPI0）は main だけが使い、A/D変換ICとジャイロ（SPI1）は割り込みだけが使います。
 * 同じSPIを両方から使わないので、通信がぶつかりません。
 */
#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "pico/time.h"

/** 走行の状態。名前は、先生のサンプル（rt2026_repeating_timer.c）に合わせてあります。 */
typedef enum {
    STATE_WAIT = 0,            /* 待ち。スイッチかシリアルの操作を待っている                 */
    STATE_SENSOR_CALIBRATION,  /* ライン・マーカーセンサーのキャリブレーション               */
    STATE_START_DELAY,         /* スイッチを押してから動き出すまでの待ち（追加した状態）     */
    STATE_SEARCH_START_MARKER, /* スタートマーカー探索                                       */
    STATE_SKIP_MARKER,         /* マーカー読み飛ばし                                         */
    STATE_TRACE_ACCEL,         /* 加速しつつライントレース                                   */
    STATE_TRACE_CONSTANT,      /* 定常速度でのライントレース                                 */
    STATE_TRACE_DECEL,         /* 減速しつつライントレース                                   */
    STATE_STOP_MOTOR,          /* モーター停止（ブレーキをかけている）                       */
    STATE_DONE,                /* 走行が終わった。main が記録を閉じるのを待っている（追加）  */
} run_state_t;

/** 走行が止まった理由。 */
typedef enum {
    STOP_NONE = 0,
    STOP_GOAL,      /* ゴールのマーカーを見つけて、減速して止まった                */
    STOP_SWITCH,    /* 走行中にスイッチが押された                                  */
    STOP_LINE_LOST, /* ラインを見失ったまま、LOST_STOP_MS が過ぎた                 */
    STOP_TIMEOUT,   /* 走り始めてから RUN_MAX_MS が過ぎた                          */
    STOP_ENCODER,   /* duty を出しているのに、車輪が回っていない（STALL_STOP_MS）か、
                       逆に回っている（REVERSE_STOP_MS）。エンコーダかモーターの向きが逆のとき、
                       配線が外れているときに、ここで止まる                         */
} stop_reason_t;

/** キャリブレーションの結果。 */
typedef enum {
    CAL_RESULT_NONE = 0, /* 新しい結果は無い                                        */
    CAL_RESULT_OK,       /* 成功                                                    */
    CAL_RESULT_NG,       /* 失敗（白と黒の差が足りないセンサーがある）。前の値に戻した */
} cal_result_t;

/** 動作確認メニューで表示するための、今の値の写し。 */
typedef struct {
    run_state_t state;
    uint16_t raw[SENSOR_NUM];
    uint8_t  norm[SENSOR_NUM];
    uint8_t  bits;
    float    displacement;
    int32_t  enc_l, enc_r;     /* エンコーダの数                */
    float    speed_l, speed_r; /* 車輪の速さ [m/s]              */
    float    vbat_v;           /* 電源電圧 [V]                  */
    float    gyro_dps;         /* 角速度 [deg/s]（USE_GYRO が1のとき） */
    uint16_t corner_count;     /* 今回の走行で見つけた、コーナーのマーカーの数 */
} control_snapshot_t;

void control_init(void);                          /* 起動時に1回呼ぶ                       */
bool control_timer_cb(repeating_timer_t *t);      /* 1msのタイマーに登録する関数           */

run_state_t   control_state(void);
stop_reason_t control_stop_reason(void);
void control_get_snapshot(control_snapshot_t *out);

/* ----- main からの依頼。どれも、STATE_WAIT のときだけ受け付けます ----- */
void control_start_calibration(void);
void control_start_run(bool recording);           /* recording は、始まりのブザーの鳴らし分けに使う */
void control_ack_done(void);                      /* STATE_DONE から STATE_WAIT に戻す      */

/** キャリブレーションが終わっていれば、その結果を返します（1回返すと CAL_RESULT_NONE に戻ります）。 */
cal_result_t control_take_cal_result(void);
void control_set_busy_led(bool on);               /* 消去の間など、LEDを速く点滅させる      */

/* ----- フラッシュメモリの確認用：1msごとにダミーの記録を n 件、log_put() に渡す ----- */
void control_logtest_start(uint32_t n);
bool control_logtest_busy(void);

/**
 * 目標の速さと角速度から、左右のモーターの duty を計算します（仕様書 4.1.5）。
 * 引数の並びは、先生のサンプルの motor_control() と同じです。
 */
void motor_control(float target_v, float accel, float vbattery, float gyro_deg_s, float target_deg_s,
                   float cur_v_l, float cur_v_r, float *duty_l, float *duty_r);

#endif /* CONTROL_H */
