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
    float    gyro_dps;         /* ジャイロの角速度 [deg/s]。オフセットを引いた値（左回りが正） */
    float    gyro_raw_dps;     /* ジャイロから読んだままの角速度 [deg/s]                       */
    float    gyro_offset_dps;  /* 前に測ったオフセット [deg/s]                                 */
    float    gyro_spread_dps;  /* オフセットを測っている間の、値の振れ幅（最大−最小）[deg/s]  */
    float    gyro_angle_deg;   /* 角速度を足し合わせた角度 [度]。オフセットを測った時と、走行の開始で 0 に戻る */
    uint16_t corner_count;     /* 今回の走行で見つけた、コーナーのマーカーの数 */
} control_snapshot_t;

/** 1msの割り込み（control_timer_cb）にかかった時間。 */
typedef struct {
    uint32_t avg_us;     /* 直前の1秒間（1000回）の平均 [us]                         */
    uint32_t max_us;     /* 前に control_take_isr_time() を呼んでからの最大 [us]      */
    uint32_t over_count; /* 同じ期間に、1000us（割り込みの間隔）を超えた回数（0が正常） */
} control_isr_time_t;

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
/** 1msの割り込みにかかった時間を返します。呼ぶと、最大と「超えた回数」は 0 から測り直しになります。 */
void control_take_isr_time(control_isr_time_t *out);
/** ジャイロのオフセットを、今から測り直してもらいます（待ちの状態のときだけ働きます。約1秒かかります）。
 *  測っている間は、機体を動かしません。測り終わると、角度も 0 に戻ります。
 *  ジャイロ（SPI1）を main から直接読まないのは、割り込みの中のA/D変換ICの通信とぶつかるためです。 */
void control_request_gyro_offset(void);
/** オフセットを測っている間（依頼を受けてから、終わるまで）true。 */
bool control_gyro_offset_busy(void);

/* ----- フラッシュメモリの確認用：1msごとにダミーの記録を n 件、log_put() に渡す ----- */
void control_logtest_start(uint32_t n);
bool control_logtest_busy(void);

#endif /* CONTROL_H */
