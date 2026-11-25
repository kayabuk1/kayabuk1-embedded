/**
 * @file  control.c
 * @brief 1msの割り込みの中身です。説明は control.h にあります。
 *
 * モーターとエンコーダは、担当の方のモジュール（encoded_geared_motor）を使います。
 *   motor_control(&motor.motor_left) : エンコーダを読み、速さを計算し、duty を計算する（1msに1回だけ呼ぶ）
 *   motor_set(左, 右)                : duty [%] をモーターに出す
 *   motor_stop() / motor_free()      : ブレーキ / 惰性
 *
 * 1回の割り込みの流れ（control_timer_cb）：
 *   1. 読む         : ラインセンサー、エンコーダ、電源電圧、（ジャイロ）、スイッチ
 *   2. 判定する     : ゴールのマーカーを通り過ぎたか、止めるべき状態になっていないか
 *   3. 状態を進める : switch (state) の中。走行中は、目標の速さと角速度からモーターを動かす
 *   4. 記録する     : 走行中は、1件を log_put() に渡す
 *   5. 知らせる     : LEDを点滅させ、見張りのタイマー（ウォッチドッグ）に「動いている」と知らせる
 */
#include "control.h"

#include <math.h>

#include "buzzer/buzzer.h"
#include "encoded_geared_motor/encoded_geared_motor.h"
#include "flash_log.h"
#include "gyro/gyro.h"
#include "hardware/adc.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "line_sensor.h"
#include "params.h"
#include "pico/stdlib.h"
#include "sw/sw.h"

/* ライン変位（0〜100）から、1m/sで走るときの目標の角速度 [deg/s] を引く表です。
 * 先生のサンプル（rt2026_repeating_timer.c）の値を、そのまま使っています。
 * 値は、「変位1につき0.4度。センサーが旋回の中心から100mmの円弧の上にある」として、
 * 仕様書 4.1.4 の式 R = da ÷ (2 sinθ) から求めた角速度（= 速さ ÷ R）と一致します
 * （例：変位50 → θ = 20度 → R = 0.146m → 1m/s ÷ 0.146m = 6.84rad/s = 391.9deg/s）。
 */
static const float targetAngularVelocity[101] = {
    0.0f,   8.0f,   16.0f,  24.0f,  32.0f,  40.0f,  48.0f,  56.0f,  64.0f,  72.0f,  79.9f,  87.9f,  95.9f,
    103.9f, 111.8f, 119.8f, 127.7f, 135.7f, 143.6f, 151.6f, 159.5f, 167.4f, 175.3f, 183.2f, 191.1f, 199.0f,
    206.9f, 214.7f, 222.6f, 230.4f, 238.2f, 246.1f, 253.9f, 261.7f, 269.5f, 277.2f, 285.0f, 292.7f, 300.4f,
    308.2f, 315.9f, 323.5f, 331.2f, 338.9f, 346.5f, 354.1f, 361.7f, 369.3f, 376.9f, 384.4f, 391.9f, 399.4f,
    406.9f, 414.4f, 421.8f, 429.3f, 436.7f, 444.1f, 451.4f, 458.8f, 466.1f, 473.4f, 480.7f, 487.9f, 495.1f,
    502.3f, 509.5f, 516.7f, 523.8f, 530.9f, 538.0f, 545.0f, 552.0f, 559.0f, 566.0f, 573.0f, 579.9f, 586.8f,
    593.6f, 600.4f, 607.2f, 614.0f, 620.8f, 627.5f, 634.1f, 640.8f, 647.4f, 654.0f, 660.5f, 667.1f, 673.6f,
    680.0f, 686.4f, 692.8f, 699.2f, 705.5f, 711.8f, 718.0f, 724.3f, 730.4f, 736.6f};

/* ブザーの鳴らし方。16ビットを上の桁から順に、1ビット50msで鳴らします（1 = 鳴る）。 */
#define BEEP_SHORT  0x8000u /* 短く1回             */
#define BEEP_SHORT2 0xA000u /* 短く2回             */
#define BEEP_LONG   0xFF00u /* 長く1回（0.4秒）    */

/* ----- 状態 ----- */
static volatile run_state_t   state = STATE_WAIT;
static volatile stop_reason_t stop_reason = STOP_NONE;
static volatile cal_result_t  cal_result = CAL_RESULT_NONE;
static uint32_t state_ms = 0; /* 今の状態に入ってからの時間 [ms] */
static uint32_t run_ms = 0;   /* 走り始めてからの時間 [ms]       */

/* ----- main からの依頼（割り込みが、STATE_WAIT のときに受け取る） ----- */
static volatile bool req_calibration = false;
static volatile bool req_run = false;
static volatile bool req_run_recording = false;
static volatile bool req_ack = false;
static volatile bool busy_led = false;

/* ----- キャリブレーション中の値 ----- */
static bool cal_sw_released = false; /* 始めるときの長押しを、一度離したか */

/* ----- 走行中の値 ----- */
static float    target_vel = 0.0f;   /* 目標の速さ [m/s]                           */
static float    target_omega = 0.0f; /* 目標の角速度 [deg/s]（左に曲がるときが正） */
static float    duty_l = 0.0f;       /* モーターに出している duty（1.0 = 100%）    */
static float    duty_r = 0.0f;
static float    skip_from_m = 0.0f;  /* マーカーの読み飛ばしを始めた地点 [m]       */
static uint32_t lost_ms = 0;         /* ラインを見失ってからの時間 [ms]            */
static uint32_t stall_ms_l = 0;      /* 左：duty を出しているのに回っていない時間 [ms] */
static uint32_t stall_ms_r = 0;      /* 右：同じ                                   */
static uint32_t rev_ms_l = 0;        /* 左：duty を出しているのに逆に回っている時間 [ms] */
static uint32_t rev_ms_r = 0;        /* 右：同じ                                   */
static bool     in_marker = false;   /* ゴール側のマーカーセンサーが、今、白を見ているか */
static bool     marker_is_cross = false; /* その白は、交差だったか                 */
static bool     prev_corner = false;
static uint16_t corner_count = 0;

/* ----- 電源電圧とジャイロ ----- */
static float    vbat_v = VBAT_NOMINAL_V;
static float    gyro_dps = 0.0f;
static volatile bool gyro_monitor = false; /* 動作確認メニューが、ジャイロの値を表示している間だけ true */
#if USE_GYRO
static float    gyro_offset = 0.0f; /* 止まっているときに読める値。差し引いて使う */
static float    gyro_sum = 0.0f;
static uint32_t gyro_n = 0;
#endif

/* ----- フラッシュメモリの確認用のダミーの記録 ----- */
static volatile uint32_t logtest_left = 0;
static uint16_t          logtest_seq = 0;

static uint32_t led_ms = 0;

/* ===== 小さな道具 =========================================================== */

static void enter(run_state_t next)
{
    state = next;
    state_ms = 0;
}

static float clampf(float x, float lo, float hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

static int16_t to_i16(float x)
{
    return (int16_t)clampf(x, -32768.0f, 32767.0f);
}

/** 走行中（モーターを動かしている状態）なら true。 */
static bool is_running(run_state_t s)
{
    return s >= STATE_SEARCH_START_MARKER && s <= STATE_TRACE_DECEL;
}

/* ===== モーターとエンコーダ（encoded_geared_motor） ========================= */

/**
 * 左右のモーターの構造体に、目標を書き込みます。
 * 次の割り込みの motor_control() が、この値から duty を計算します。
 */
static void set_motor_targets(float target_v, float accel, float target_deg_s)
{
    Motor_Unit *const units[2] = {&motor.motor_left, &motor.motor_right};
    for (int i = 0; i < 2; i++) {
        units[i]->control.target_v = target_v;
        units[i]->control.accel = accel;
        units[i]->control.target_deg_s = target_deg_s;
    }
}

/** 走った距離 [m]。左右のエンコーダの数の平均を、1mあたりの数（PULSE_COUNT_1M）で割ります。 */
static float distance_m(void)
{
    return (float)(motor.motor_left.encode.encode_value + motor.motor_right.encode.encode_value) * 0.5f / PULSE_COUNT_1M;
}

/** モーターにブレーキをかけ、走行中の値を0に戻します。 */
static void brake_now(void)
{
    motor_stop();
    set_motor_targets(0.0f, 0.0f, 0.0f);
    duty_l = 0.0f;
    duty_r = 0.0f;
    target_vel = 0.0f;
}

/* ===== 電源電圧 ============================================================= */

static void battery_update(void)
{
    /* Picoの中のA/D変換は12ビット（0〜4095）で、3.3Vが4095です。
     * 分圧（10kΩ と 2.2kΩ）で 2.2/12.2 倍になっているので、12.2/2.2 倍して元に戻します。
     */
    const float v_pin = (float)adc_read() * 3.3f / 4095.0f;
    const float v = v_pin * (VBAT_R_TOP + VBAT_R_BOTTOM) / VBAT_R_BOTTOM;
    vbat_v += (v - vbat_v) * (1.0f / 16.0f); /* 16回分をならす */
}

/* ===== 走行の1ms分 ========================================================== */

/**
 * 目標の速さを accel で変え、ラインをたどるようにモーターを動かします（1ms分）。
 * duty の計算は、この割り込みの最初に呼んだ motor_control()（担当の方の関数）が済ませています。
 * ここでは、その duty を上限（DUTY_LIMIT）に収めてモーターに出し、次の回のための目標を書き込みます。
 */
static void trace_step(float accel)
{
    /* duty は％（-100〜+100）です。 */
    const float limit = DUTY_LIMIT * 100.0f;
    const float percent_l = clampf(motor.motor_left.control.duty, -limit, limit);
    const float percent_r = clampf(motor.motor_right.control.duty, -limit, limit);
    motor_set(percent_l, percent_r);
    duty_l = percent_l * 0.01f;
    duty_r = percent_r * 0.01f;

    target_vel = clampf(target_vel + accel * 0.001f, 0.0f, RUN_SPEED_MPS);
    set_motor_targets(target_vel, accel, target_omega);
}

/* ===== マーカー ============================================================= */

/**
 * ゴール側のマーカー（sensor[6]）を通り過ぎた瞬間に、1回だけ true を返します。
 * 交差では、横切るラインを、左右のマーカーセンサーとラインセンサーの全部が見ます。
 * sensor[6] が白を見ている間に、コーナー側（sensor[7]）も白になるか、ラインセンサーの
 * 5個以上が白になったら、交差と見なして、マーカーとしては数えません。
 */
static bool goal_marker_passed(void)
{
    const bool goal = line_is_white(SENSOR_GOAL);
    const bool corner = line_is_white(SENSOR_CORNER);

    if (goal) {
        if (!in_marker) {
            in_marker = true;
            marker_is_cross = false;
        }
        if (corner || line_white_count() >= 5) {
            marker_is_cross = true;
        }
        return false;
    }
    if (in_marker) {
        in_marker = false;
        return !marker_is_cross;
    }
    return false;
}

/* ===== 1msの割り込み ======================================================== */

bool control_timer_cb(repeating_timer_t *t)
{
    (void)t;
    gpio_put(PIN_CHECK, 1); /* チェック端子。Hの時間が、この割り込みにかかった時間です */

    /* ---- 1. 読む ---- */
    line_sensor_sample();
    battery_update();
#if USE_GYRO
    gyro_dps = gyro_read_z_dps() - gyro_offset;
#else
    /* 走行に使わない設定でも、動作確認メニューの 8 を開いている間は読みます（表示のため）。 */
    if (gyro_monitor) {
        gyro_dps = gyro_read_z_dps();
    }
#endif
    sw_update_1ms();
    beepProcess();

    /* エンコーダを読み、車輪の速さと duty を計算します（担当の方の motor_control()）。
     * duty は、前の回の割り込みで書き込んだ目標から計算されます。モーターに出すのは、走行中だけです。
     * 電源電圧は、測った値を渡します。電池をつないでいないとき（USBだけ）は、ほぼ0と読めるので、
     * 定格の値を渡します。
     */
    {
        const float vb = (vbat_v < 5.0f) ? VBAT_NOMINAL_V : vbat_v;
        motor.motor_left.control.Vbattery = vb;
        motor.motor_right.control.Vbattery = vb;
#if USE_GYRO
        motor.motor_left.control.gyro_deg_s = gyro_dps;
        motor.motor_right.control.gyro_deg_s = gyro_dps;
#else
        /* ジャイロを使わないときは、「目標どおりに旋回している」と渡します。
         * motor_control() の中の旋回の補正（目標の角速度 － ジャイロの角速度）が、0になります。
         */
        motor.motor_left.control.gyro_deg_s = motor.motor_left.control.target_deg_s;
        motor.motor_right.control.gyro_deg_s = motor.motor_right.control.target_deg_s;
#endif
        motor_control(&motor.motor_left);
        motor_control(&motor.motor_right);
    }

    const float disp = line_displacement();
    const float v_l = motor.motor_left.control.cur_v;
    const float v_r = motor.motor_right.control.cur_v;
    const float v = (v_l + v_r) * 0.5f;

    /* ---- 2. 判定する ---- */
    const bool goal_passed = goal_marker_passed();
    const bool corner = line_is_white(SENSOR_CORNER);
    if (is_running(state) && corner && !prev_corner && line_white_count() < 5) {
        corner_count++;
    }
    prev_corner = corner;

    /* 目標の角速度。表は「1m/sのとき」の値なので、今の速さを掛けます。
     * ずれが正（機体がラインの右）のとき、左に曲がる向き（正）にします。
     * 動き出しは速さが0に近く、曲がれなくなるので、0.1m/sを下限にしています。
     */
    {
        int idx = (int)fabsf(disp);
        if (idx > 100) {
            idx = 100;
        }
        const float sign = (disp >= 0.0f) ? 1.0f : -1.0f;
        const float v_ref = (v > 0.1f) ? v : 0.1f;
        target_omega = sign * targetAngularVelocity[idx] * v_ref * OMEGA_GAIN;
    }

    /* ---- 3. 状態を進める ---- */
    state_ms++;

    /* 安全のための停止。動き出す前の待ちの間と、走行中に確かめます。 */
    if (state == STATE_START_DELAY || is_running(state)) {
        stop_reason_t why = STOP_NONE;
        if (is_running(state)) {
            run_ms++;
            lost_ms = line_is_lost() ? lost_ms + 1 : 0;
            /* duty を出しているのに、車輪が回っていない（または逆に回っている）時間を数えます。
             * エンコーダの向きが逆だと、速さの制御が「遅い」と判断して duty を上げるので、
             * 目標より速く走ります。それを、ここで止めます。
             */
            stall_ms_l = (duty_l > STALL_DUTY && v_l < STALL_SPEED_MPS) ? stall_ms_l + 1 : 0;
            stall_ms_r = (duty_r > STALL_DUTY && v_r < STALL_SPEED_MPS) ? stall_ms_r + 1 : 0;
            rev_ms_l = (duty_l > STALL_DUTY && v_l < -REVERSE_SPEED_MPS) ? rev_ms_l + 1 : 0;
            rev_ms_r = (duty_r > STALL_DUTY && v_r < -REVERSE_SPEED_MPS) ? rev_ms_r + 1 : 0;
            if (run_ms >= RUN_MAX_MS) {
                why = STOP_TIMEOUT;
            }
            if (lost_ms >= LOST_STOP_MS) {
                why = STOP_LINE_LOST;
            }
            if (stall_ms_l >= STALL_STOP_MS || stall_ms_r >= STALL_STOP_MS || rev_ms_l >= REVERSE_STOP_MS ||
                rev_ms_r >= REVERSE_STOP_MS) {
                why = STOP_ENCODER;
            }
        }
        if (sw_is_pressed(0) || sw_is_pressed(1)) {
            why = STOP_SWITCH;
            sw_ignore_current_press(); /* 止めるために押したスイッチを、次の操作として数えない */
        }
        if (why != STOP_NONE) {
            brake_now();
            stop_reason = why;
            setBeepPattern(BUZZER_PIN, BEEP_LONG, DO_6);
            enter(STATE_STOP_MOTOR);
        }
    }

    switch (state) {
    case STATE_WAIT:
        req_ack = false;
        if (req_calibration) {
            req_calibration = false;
            cal_sw_released = false;
            line_cal_begin();
            setBeepPattern(BUZZER_PIN, BEEP_SHORT, DO_6);
            enter(STATE_SENSOR_CALIBRATION);
        } else if (req_run) {
            req_run = false;
            stop_reason = STOP_NONE;
#if USE_GYRO
            gyro_sum = 0.0f;
            gyro_n = 0;
#endif
            /* 記録ありなら高い音で短く1回、記録なしなら低い音で短く2回。 */
            if (req_run_recording) {
                setBeepPattern(BUZZER_PIN, BEEP_SHORT, DO_7);
            } else {
                setBeepPattern(BUZZER_PIN, BEEP_SHORT2, DO_6);
            }
            enter(STATE_START_DELAY);
        } else if (logtest_left > 0) {
            /* フラッシュメモリの確認用。電源電圧の欄に連番を入れた、ダミーの記録を渡します。 */
            now_sensors_data.sensor_displacement = -15;
            now_sensors_data.sensor_binary = 0xF0;
            now_sensors_data.target_speed = 1234;
            now_sensors_data.left_speed = -255;
            now_sensors_data.right_speed = 255;
            now_sensors_data.left_motor_duty = 15;
            now_sensors_data.right_motor_duty = -16;
            now_sensors_data.target_angular_velocity = -256;
            now_sensors_data.now_angular_velocity = 255;
            now_sensors_data.vcc_mv = logtest_seq++;
            log_put(&now_sensors_data);
            logtest_left = logtest_left - 1;
        }
        break;

    case STATE_SENSOR_CALIBRATION: {
        line_cal_update();
        /* 始めるときの長押しを一度離した後で、もう一度スイッチ0を押したら、そこで終わります。
         * 押さなくても、CAL_MS が過ぎたら終わります。
         */
        if (!sw_is_pressed(0)) {
            cal_sw_released = true;
        }
        const bool end_by_sw = cal_sw_released && sw_is_pressed(0);
        if (end_by_sw || state_ms >= CAL_MS) {
            if (end_by_sw) {
                sw_ignore_current_press();
            }
            /* 成功なら高い音で短く2回、失敗（白と黒の差が小さいセンサーがある）なら低い音で長く1回。 */
            if (line_cal_end()) {
                cal_result = CAL_RESULT_OK;
                setBeepPattern(BUZZER_PIN, BEEP_SHORT2, DO_7);
            } else {
                cal_result = CAL_RESULT_NG;
                setBeepPattern(BUZZER_PIN, BEEP_LONG, DO_6);
            }
            enter(STATE_WAIT);
        }
        break;
    }

    case STATE_START_DELAY:
#if USE_GYRO
        /* 機体が止まっている間に、ジャイロの値の平均を取り、0点のずれとして覚えます。 */
        gyro_sum += gyro_dps + gyro_offset;
        gyro_n++;
#endif
        if (state_ms >= START_DELAY_MS) {
#if USE_GYRO
            gyro_offset = gyro_sum / (float)gyro_n;
#endif
            target_vel = 0.0f;
            set_motor_targets(0.0f, 0.0f, 0.0f);
            duty_l = 0.0f;
            duty_r = 0.0f;
            run_ms = 0;
            lost_ms = 0;
            stall_ms_l = 0;
            stall_ms_r = 0;
            rev_ms_l = 0;
            rev_ms_r = 0;
            in_marker = false;
            corner_count = 0;
            setBeepPattern(BUZZER_PIN, BEEP_SHORT, FA_6);
            enter(STATE_SEARCH_START_MARKER);
        }
        break;

    case STATE_SEARCH_START_MARKER:
        trace_step(RUN_ACCEL_MPS2);
        if (goal_passed) {
            skip_from_m = distance_m();
            setBeepPattern(BUZZER_PIN, BEEP_SHORT, DO_6);
            enter(STATE_SKIP_MARKER);
        }
        break;

    case STATE_SKIP_MARKER:
        trace_step(RUN_ACCEL_MPS2);
        if (distance_m() - skip_from_m > SKIP_MARKER_M) {
            enter(STATE_TRACE_ACCEL);
        }
        break;

    case STATE_TRACE_ACCEL:
        trace_step(RUN_ACCEL_MPS2);
        if (goal_passed) {
            setBeepPattern(BUZZER_PIN, BEEP_SHORT, DO_6);
            enter(STATE_TRACE_DECEL);
        } else if (target_vel >= RUN_SPEED_MPS) {
            enter(STATE_TRACE_CONSTANT);
        }
        break;

    case STATE_TRACE_CONSTANT:
        trace_step(0.0f);
        if (goal_passed) {
            setBeepPattern(BUZZER_PIN, BEEP_SHORT, DO_6);
            enter(STATE_TRACE_DECEL);
        }
        break;

    case STATE_TRACE_DECEL:
        trace_step(-RUN_DECEL_MPS2);
        if (target_vel <= 0.05f) {
            brake_now();
            stop_reason = STOP_GOAL;
            enter(STATE_STOP_MOTOR);
        }
        break;

    case STATE_STOP_MOTOR:
        if (state_ms >= STOP_HOLD_MS) {
            motor_free();
            enter(STATE_DONE);
        }
        break;

    case STATE_DONE:
        if (req_ack) {
            req_ack = false;
            enter(STATE_WAIT);
        }
        break;

    default:
        break;
    }

    /* ---- 4. 記録する ---- */
    if (is_running(state) || state == STATE_STOP_MOTOR) {
        now_sensors_data.sensor_displacement = (int8_t)disp;
        now_sensors_data.sensor_binary = line_bits();
        now_sensors_data.target_speed = to_i16(target_vel * 1000.0f);
        now_sensors_data.left_speed = to_i16(v_l * 1000.0f);
        now_sensors_data.right_speed = to_i16(v_r * 1000.0f);
        now_sensors_data.left_motor_duty = (int8_t)(duty_l * 100.0f);
        now_sensors_data.right_motor_duty = (int8_t)(duty_r * 100.0f);
        now_sensors_data.target_angular_velocity = to_i16(target_omega);
#if USE_GYRO
        now_sensors_data.now_angular_velocity = to_i16(gyro_dps);
#else
        /* ジャイロを使わないときは、左右の車輪の速さの差から計算した角速度 [deg/s] を記録します
         * （左に曲がるときが正）。目標の角速度と比べると、曲がり方が足りているかが分かります。
         */
        now_sensors_data.now_angular_velocity = to_i16((v_r - v_l) / TREAD * (180.0f / 3.14159265f));
#endif
        now_sensors_data.vcc_mv = (uint16_t)clampf(vbat_v * 1000.0f, 0.0f, 65535.0f);
        log_put(&now_sensors_data); /* 記録中でなければ、何もせずに戻ります */
    }

    /* ---- LED ----
     * Picoの基板の上のLED：いつも0.5秒ごとに点滅。点滅していれば、この割り込みは動いています。
     * LED0：待ちは0.5秒ごと、キャリブレーションと消去は速い点滅、走行中は点きっぱなし。
     */
    led_ms++;
    gpio_put(PIN_LED_ONBOARD, (led_ms / 500u) % 2u);
    if (is_running(state) || state == STATE_START_DELAY) {
        gpio_put(PIN_LED0, 1);
    } else {
        const uint32_t half = (state == STATE_SENSOR_CALIBRATION || busy_led) ? 60u : 500u;
        gpio_put(PIN_LED0, (led_ms / half) % 2u);
    }

#if USE_WATCHDOG
    /* 見張りのタイマーに、「割り込みは最後まで動いた」と知らせます（残り時間が WATCHDOG_MS に戻ります）。
     * 残り時間が0になると、マイコンがリセットされます。
     *
     * ただし、残り時間が WATCHDOG_MS の半分より少ないときは、知らせません。そうなるのは、次の2つの場合です。
     *   1. 割り込みが、WATCHDOG_MS の半分より長く止まっていた（異常なので、そのままリセットさせます）
     *   2. PCからの書き込み（picotool の -f）や watchdog_reboot() が、再起動のために、このタイマーを
     *      短い時間（10ms や 100ms）に設定し直した（ここで知らせると、残り時間が戻って、再起動が始まりません）
     * 残り時間は、制御レジスタの下24ビットに、µs の単位で入っています（RP2350）。
     */
    if ((watchdog_hw->ctrl & WATCHDOG_CTRL_TIME_BITS) > (uint32_t)WATCHDOG_MS * 1000u / 2u) {
        watchdog_update();
    }
#endif

    gpio_put(PIN_CHECK, 0);
    return true;
}

/* ===== main から呼ぶ関数 ==================================================== */

void control_init(void)
{
    gpio_init(PIN_LED0);
    gpio_set_dir(PIN_LED0, GPIO_OUT);
    gpio_init(PIN_LED_ONBOARD);
    gpio_set_dir(PIN_LED_ONBOARD, GPIO_OUT);
    gpio_init(PIN_CHECK);
    gpio_set_dir(PIN_CHECK, GPIO_OUT);

    /* 電源電圧。Picoの中のA/D変換を使うのは、ここだけです。 */
    adc_init();
    adc_gpio_init(PIN_VBAT);
    adc_select_input(VBAT_ADC_CH);
}

run_state_t control_state(void)
{
    return state;
}

stop_reason_t control_stop_reason(void)
{
    return stop_reason;
}

cal_result_t control_take_cal_result(void)
{
    if (cal_result == CAL_RESULT_NONE) {
        return CAL_RESULT_NONE; /* 新しい結果が無いときは、割り込みを止めずに戻る */
    }
    const uint32_t ints = save_and_disable_interrupts();
    const cal_result_t r = cal_result;
    cal_result = CAL_RESULT_NONE;
    restore_interrupts(ints);
    return r;
}

void control_get_snapshot(control_snapshot_t *out)
{
    /* 写している途中に割り込みが入ると、新旧の値が混ざります。割り込みを止めている間に写します。 */
    const uint32_t ints = save_and_disable_interrupts();
    out->state = state;
    for (int i = 0; i < SENSOR_NUM; i++) {
        out->raw[i] = line_raw(i);
        out->norm[i] = line_norm(i);
    }
    out->bits = line_bits();
    out->displacement = line_displacement();
    out->enc_l = motor.motor_left.encode.encode_value;
    out->enc_r = motor.motor_right.encode.encode_value;
    out->speed_l = motor.motor_left.control.cur_v;
    out->speed_r = motor.motor_right.control.cur_v;
    out->vbat_v = vbat_v;
    out->gyro_dps = gyro_dps;
    out->corner_count = corner_count;
    restore_interrupts(ints);
}

void control_start_calibration(void)
{
    req_calibration = true;
}

void control_start_run(bool recording)
{
    req_run_recording = recording;
    req_run = true;
}

void control_ack_done(void)
{
    req_ack = true;
}

void control_set_busy_led(bool on)
{
    busy_led = on;
}

void control_set_gyro_monitor(bool on)
{
    gyro_monitor = on;
}

void control_logtest_start(uint32_t n)
{
    const uint32_t ints = save_and_disable_interrupts();
    logtest_seq = 0;
    logtest_left = n;
    restore_interrupts(ints);
}

bool control_logtest_busy(void)
{
    return logtest_left > 0;
}
