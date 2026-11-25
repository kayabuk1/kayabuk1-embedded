#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "rt2026_buzzer.h"
#include "rt2026_digital_io.h"
#include "rt2026_spi.h"
#include "rt2026_pwm.h"
#include "rt2026_encoder.h"
#include "rt2026_flush.h"

#define ACCEL 2.f      // 加速度m/s2
#define STD_SPEED 0.7f // 定常速度上限

extern int16_t gyro[];
extern int16_t line_marker[];
float displacementForLine = 0.0f;
odom_enc_t enc;

static struct analogSensor sensor[LINE_SENSOR_NUM + MARKER_SENSOR_NUM] = {{0,
                                                                           9999, 0, 0, 0.f},
                                                                          {0, 9999, 0, 0, 0.f},
                                                                          {0, 9999, 0, 0, 0.f},
                                                                          {0,
                                                                           9999, 0, 0, 0.f},
                                                                          {0, 9999, 0, 0, 0.f},
                                                                          {0, 9999, 0, 0, 0.f},
                                                                          {0,
                                                                           9999, 0, 0, 0.f},
                                                                          {0, 9999, 0, 0, 0.f}};

extern int left_enc_count, right_enc_count;
extern char log_condition;

typedef enum
{
    STATE_WAIT_FLASH_CLEAR = 0, // フラッシュメモリクリア待ち
    STATE_WAIT_BUTTON_1,        // 押しボタンスイッチ押下待ち
    STATE_SENSOR_CALIBRATION,   // ライン・マーカセンサキャリブレーション
    STATE_WAIT_BUTTON_2,        // 押しボタンスイッチ押下待ち
    STATE_SEARCH_START_MARKER,  // スタートマーカ探索
    STATE_SEARCH_GOAL_MARKER,   // ゴールマーカ探索
    STATE_SKIP_MARKER,          // マーカ読み飛ばし
    STATE_TRACE_ACCEL,          // 加速しつつライントレース
    STATE_TRACE_CONSTANT,       // 定常速度でのライントレース
    STATE_TRACE_DECEL,          // 減速しつつライントレース
    STATE_STOP_MOTOR,           // モータ停止
    STATE_WAIT_LOG_TRANSFER     // 待機・ログ転送
} state_t;

// ライン変位からジャイロ（MPU-6500）角速度換算（1m/s時）
const static float targetAngularVelocity[] = {0.0, 8.0, 16.0, 24.0, 32.0, 40.0, 48.0,
                                              56.0, 64.0, 72.0, 79.9, 87.9, 95.9, 103.9, 111.8, 119.8, 127.7, 135.7,
                                              143.6, 151.6, 159.5, 167.4, 175.3, 183.2, 191.1, 199.0, 206.9, 214.7,
                                              222.6, 230.4, 238.2, 246.1, 253.9, 261.7, 269.5, 277.2, 285.0, 292.7,
                                              300.4, 308.2, 315.9, 323.5, 331.2, 338.9, 346.5, 354.1, 361.7, 369.3,
                                              376.9, 384.4, 391.9, 399.4, 406.9, 414.4, 421.8, 429.3, 436.7, 444.1,
                                              451.4, 458.8, 466.1, 473.4, 480.7, 487.9, 495.1, 502.3, 509.5, 516.7,
                                              523.8, 530.9, 538.0, 545.0, 552.0, 559.0, 566.0, 573.0, 579.9, 586.8,
                                              593.6, 600.4, 607.2, 614.0, 620.8, 627.5, 634.1, 640.8, 647.4, 654.0,
                                              660.5, 667.1, 673.6, 680.0, 686.4, 692.8, 699.2, 705.5, 711.8, 718.0,
                                              724.3, 730.4, 736.6};

bool repeating_timer_callback(__unused struct repeating_timer *t)
{
    static int i;
    static state_t state = STATE_WAIT_FLASH_CLEAR;
    static char checked_line_marker_sensor = 0; // コース上の黒白状態を読み取り前0、読み取り後：1
    // static int pattern = 0;
    static int cnt1ms = 0;
    static int cnt1ms2 = 0;
    static unsigned char ledstate = 0;
    static float duty_l, duty_r;
    static int enc_past;
    static float target_vel = 0.f;
    static uint32_t time_ms = 0;
    log_record_t rec;
    static float accel_rate = 0; // 加速度[m/s2]

    set_debug_signal(true);
    mpu9250_read_gyro(gyro, PIN_CS0);

    set_led_switch(true);
    busy_wait_us(30); // インターバルタイマ上でsleep_us()を実行すると支障あり
    mcp3008_read_line_marker_sensor(line_marker, PIN_CS1);
    set_led_switch(false);

    // 暫定処理、A/D変換後に配列sensorへ直接代入する
    for (i = 0; i < 8; i++)
    {
        sensor[i].present = (unsigned short)line_marker[i];
    }

    cnt1ms++;
    cnt1ms2++;

    // エンコーダ関係アップデート
    encoder_update(&enc.left, left_enc_count);
    encoder_update(&enc.right, right_enc_count);

    int32_t left_vel = encoder_get_velocity(&enc.left);
    int32_t right_vel = encoder_get_velocity(&enc.right);

    beepProcess();

    if (cnt1ms >= 500)
    {
        cnt1ms = 0;
        ledstate ^= 1;
        outLed_onbrd(ledstate);
    }

    //	7	ライン変位取得，ライン・マーカセンサデジタル値更新
    int sign_dist = 1;
    displacementForLine = getDisplacementForLine(sensor) * 0.32f;
    if (displacementForLine < 0.f)
    {
        sign_dist = -1;
    }
    if (displacementForLine > 100.f)
    {
        displacementForLine = 100.f;
    }
    if (displacementForLine < -100.f)
    {
        displacementForLine = -100.f;
    }

    // 目標角速度設定
    float target_omega = sign_dist * targetAngularVelocity[sign_dist * (int)displacementForLine] * (left_vel * ENC_TO_VEL + right_vel * ENC_TO_VEL) / 2.f;

    // ログバッファへデータ格納
    if (log_condition == 10)
    {
        rec.sensor_disp = (int)displacementForLine;
        rec.sensor_info = sensor[7].digital << 7 | sensor[6].digital << 6 | sensor[5].digital << 5 | sensor[4].digital << 4 | sensor[3].digital << 3 | sensor[2].digital << 2 | sensor[1].digital << 1 | sensor[0].digital;
        rec.target_speed = (int)(target_vel * 1000.f);
        rec.left_speed = (int)(left_vel * ENC_TO_VEL * 1000.f);
        rec.right_speed = (int)(right_vel * ENC_TO_VEL * 1000.f);
        rec.left_duty = (int)(duty_l * 100.f);
        rec.right_duty = (int)(duty_r * 100.f);
        rec.target_omega = (int)target_omega;
        rec.omega = (int)gyro[2] / 16.4f;
        rec.vbat = 7400;
        rec.reserved[0] = state;

        log_add_from_isr(&rec, time_ms);

        time_ms += 2;
    }
    // ログバッファへデータ格納　処理終了

    switch (state)
    {
    case 0:
        if (log_condition == 1) // フラッシュメモリ準備ができたら遷移
        {
            cnt1ms2 = 0;
            outLed4bit(0x01);
            setBeepPattern(BUZZER_PIN, 0xca0, DO_6);
            state = STATE_WAIT_BUTTON_1;
        }
        break;
    case STATE_WAIT_BUTTON_1:
        if (getTactSW0())
        {
            cnt1ms2 = 0;
            outLed4bit(0x02);
            setBeepPattern(BUZZER_PIN, 0x8000, DO_6);
            state = STATE_SENSOR_CALIBRATION;
        }
        break;
    // ライン・マーカセンサ最大最小値取得処理
    case STATE_SENSOR_CALIBRATION:
        for (i = 0; i < LINE_SENSOR_NUM + MARKER_SENSOR_NUM; i++)
        {
            if (sensor[i].present > sensor[i].Max)
            { // センサ最大値更新
                sensor[i].Max = sensor[i].present;
            }
            if (sensor[i].present < sensor[i].Min)
            { // 　〃　最小値更新
                sensor[i].Min = sensor[i].present;
            }
        }

        if (getTactSW0() && (cnt1ms2 >= 500))
        {
            cnt1ms2 = 0;
            outLed4bit(0x03);
            setBeepPattern(BUZZER_PIN, 0x8800, RE_6);
            state = STATE_WAIT_BUTTON_2;
        }
        break;
    case STATE_WAIT_BUTTON_2:
        if (cnt1ms2 >= 500)
        {
            cnt1ms2 = 0;
            outLed4bit(0x03);
            setBeepPattern(BUZZER_PIN, 0x8000, FA_6);
            state = STATE_SEARCH_START_MARKER;
            checked_line_marker_sensor = 1; //
            enc_past = (enc.left.count + enc.left.count) / 2;
            target_vel = 0.0f;
            accel_rate = 1.0f;
            log_condition = 10; // ログ収集開始
        }
        break;
    case STATE_SEARCH_START_MARKER:
        target_vel += accel_rate / 500.f;
        if (target_vel > STD_SPEED)
        {
            target_vel = STD_SPEED;
        }
        motor_control(target_vel, accel_rate, 7.4f, gyro[2] * (2000.0f / 32768.0f), target_omega, left_vel * ENC_TO_VEL, right_vel * ENC_TO_VEL, &duty_l, &duty_r);
        motor_set(duty_l, duty_r); // モータ駆動
        if (sensor[0].digital == 1)
        {
            enc_past = (enc.left.count + enc.left.count) / 2;
            setBeepPattern(BUZZER_PIN, 0x8000, DO_6);
            state = STATE_SKIP_MARKER;
        }
        break;
    case STATE_SKIP_MARKER:
        target_vel += accel_rate / 500.f;
        if (target_vel > STD_SPEED)
        {
            target_vel = STD_SPEED;
        }
        motor_control(target_vel, accel_rate, 7.4f, gyro[2] * (2000.0f / 32768.0f), target_omega, left_vel * ENC_TO_VEL, right_vel * ENC_TO_VEL, &duty_l, &duty_r);
        motor_set(duty_l, duty_r);                                  // モータ駆動
        if ((enc.left.count + enc.left.count) / 2 - enc_past > 984) // 30mm（マーカ幅19mm）
        {
            enc_past = (enc.left.count + enc.left.count) / 2;
            state = STATE_TRACE_ACCEL;
        }
        break;

    case STATE_TRACE_ACCEL:
        target_vel += accel_rate / 500.f;
        if (target_vel > STD_SPEED)
        {
            target_vel = STD_SPEED;
        }
        motor_control(target_vel, accel_rate, 7.4f, gyro[2] * (2000.0f / 32768.0f), target_omega, left_vel * ENC_TO_VEL, right_vel * ENC_TO_VEL, &duty_l, &duty_r);
        motor_set(duty_l, duty_r); // モータ駆動

        if (target_vel >= STD_SPEED)
        {
            cnt1ms2 = 0;
            outLed4bit(0x04);
            setBeepPattern(BUZZER_PIN, 0x8000, MI_6);
            enc_past = (enc.left.count + enc.left.count) / 2;
            accel_rate = 0.f;
            state = STATE_TRACE_CONSTANT;
        }
        break;

    case STATE_TRACE_CONSTANT:
        motor_control(target_vel, accel_rate, 7.4f, gyro[2] * (2000.0f / 32768.0f), target_omega, left_vel * ENC_TO_VEL, right_vel * ENC_TO_VEL, &duty_l, &duty_r);
        motor_set(duty_l, duty_r); // モータ駆動

        if (sensor[0].digital == 1)
        {
            cnt1ms2 = 0;
            outLed4bit(0x05);
            setBeepPattern(BUZZER_PIN, 0x8000, DO_6);
            accel_rate = -0.5f;
            state = STATE_TRACE_DECEL;
        }
        break;
    
    case STATE_TRACE_DECEL:
        target_vel += accel_rate / 500.f;
        motor_control(target_vel, accel_rate, 7.4f, gyro[2] * (2000.0f / 32768.0f), target_omega, left_vel * ENC_TO_VEL, right_vel * ENC_TO_VEL, &duty_l, &duty_r);
        motor_set(duty_l, duty_r); // モータ駆動

        if (target_vel <= 0.05f)
        {
            cnt1ms2 = 0;
            outLed4bit(0x08);
            setBeepPattern(BUZZER_PIN, 0xe880, FA_6);
            motor_stop(); // モータストップ
            state = STATE_STOP_MOTOR;
        }
        break;
    case STATE_STOP_MOTOR:
        if (cnt1ms2 >= 500)
        {
            log_condition = 99; // ログ取得終了
            motor_stop();       // モータストップ
            state = STATE_WAIT_LOG_TRANSFER;
        }
        break;
    case STATE_WAIT_LOG_TRANSFER:
        break;
    default:
    }

    set_debug_signal(false);

    return true;
}