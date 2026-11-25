/**
 * @file  selftest.c
 * @brief 基板の動作確認メニューです。新しい基板を組んだら、走らせる前に、番号の順に確かめます。
 *
 * センサーの値は、1msの割り込み（control.c）が読み続けています。
 * このメニューは、その値の写し（control_get_snapshot）を表示するだけです。
 * モーターだけは、このメニューが直接動かします（割り込みは、待ちの状態ではモーターに触りません）。
 */
#include "selftest.h"

#include <stdio.h>

#include "adc_mcp3208or3008/adc_mcp3208or3008.h"
#include "at25sf.h"
#include "buzzer/buzzer.h"
#include "control.h"
#include "encoded_geared_motor/encoded_geared_motor.h"
#include "flash_log.h"
#include "gyro/gyro.h"
#include "line_sensor.h"
#include "params.h"
#include "pico/stdlib.h"
#include "sw/sw.h"

#define WATCH_INTERVAL_MS 200  /* 値を表示し直す間隔 [ms]            */
#define JOG_DUTY          20.0f /* モーターの確認で出す duty [%]      */
#define JOG_MS            500   /* モーターの確認で回す時間 [ms]      */
#define LOGTEST_RECORDS   1000u /* フラッシュの確認で書く、ダミーの記録の件数 */

/** キーが押されていれば、その文字を返します。押されていなければ -1 を返します。 */
static int key_now(void)
{
    const int c = getchar_timeout_us(0);
    return (c == PICO_ERROR_TIMEOUT) ? -1 : c;
}

/** ms だけ待ちます。その間にキーが押されたら true を返して、すぐ戻ります。flash_task も回します。 */
static bool wait_or_key(uint32_t ms)
{
    const absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) {
        flash_task();
        if (key_now() >= 0) {
            return true;
        }
    }
    return false;
}

/** 何かキーが押されるまで待って、その文字を返します。 */
static int wait_key(void)
{
    int c;
    do {
        flash_task();
        /* メニューの中では、スイッチでの操作（走行、キャリブレーション、全消去）を受け付けません。
         * 押されたら、そのことを知らせます。
         */
        if (sw_take_event(0) != SW_EV_NONE || sw_take_event(1) != SW_EV_NONE) {
            printf("（動作確認メニューの中では、スイッチでの操作は働きません。q で戻ってから、押してください。）\n");
        }
        c = key_now();
    } while (c < 0);
    return c;
}

static void test_switch_led(void)
{
    printf("スイッチ0（GP%d）とスイッチ1（GP%d）を押してください。押している間、1 と表示されます。\n", PIN_SW0, PIN_SW1);
    printf("LED0（GP%d）と、Picoの基板の上のLEDは、0.5秒ごとに点滅しているはずです。何かキーを押すと戻ります。\n", PIN_LED0);
    printf("  Picoの基板の上のLEDだけが点滅する → LED0 の配線か、PIN_LED0（board.h）を確かめる\n");
    printf("  押しても 0 のまま、または、離した後も 1 のまま → board.h の SW_PRESSED_LEVEL を逆にする\n");
    while (!wait_or_key(WATCH_INTERVAL_MS)) {
        printf("SW0 = %d  SW1 = %d\n", sw_is_pressed(0) ? 1 : 0, sw_is_pressed(1) ? 1 : 0);
    }
    /* 確かめるために押した分の「短押し」「長押し」を捨てます（戻った後に、走行や消去が始まらないように）。 */
    (void)sw_take_event(0);
    (void)sw_take_event(1);
}

static void test_buzzer(void)
{
    printf("ブザー（GP%d）を、ド・ミ・ソの順に鳴らします。\n", PIN_BUZZER);
    const scaleSelect notes[3] = {DO_6, MI_6, SO_6};
    for (int i = 0; i < 3; i++) {
        setBeepPattern(BUZZER_PIN, 0xF000u, notes[i]); /* 0.2秒 */
        sleep_ms(400);
    }
}

static void test_line_sensor(void)
{
    printf("ラインセンサー。機体をラインの上で左右に動かしてください。何かキーを押すと戻ります。\n");
    printf("  raw  = A/D変換の値（白いほど小さい）、norm = 正規化した値（黒0〜白100）\n");
    printf("  並びは sensor[0]（右端）〜[5]（左端）、[6]（ゴール側マーカー）、[7]（コーナー側マーカー）\n");
    printf("  全部の raw が同じ値のまま動かなければ、LEDのスイッチ（GP%d）の向き（board.h の LINE_LED_ON_LEVEL）を疑います。\n",
           PIN_LINE_LED);
    printf("  並びが違うときは board.h の SENSOR_ADC_CH を、ずれの正負が逆のときは LINE_DISP_SIGN を直します。\n");
    printf("  raw は 0〜%u の値です（ICが MCP3208 でも、今の設定のままで、上の10ビットを読みます）。\n",
           (unsigned)(MCP3208or3008_RESOLUTION - 1u));
    control_snapshot_t s;
    while (!wait_or_key(WATCH_INTERVAL_MS)) {
        control_get_snapshot(&s);
        printf("raw");
        for (int i = 0; i < SENSOR_NUM; i++) {
            printf(" %4u", (unsigned)s.raw[i]);
        }
        printf(" | norm");
        for (int i = 0; i < SENSOR_NUM; i++) {
            printf(" %3u", (unsigned)s.norm[i]);
        }
        printf(" | 白黒 ");
        for (int i = SENSOR_NUM - 1; i >= 0; i--) {
            printf("%d", (s.bits >> i) & 1);
        }
        printf(" | ずれ %+6.1f\n", (double)s.displacement);
    }
    printf("覚えている白と黒の値（キャリブレーションの結果。まだなら、仕様書の値）\n");
    for (int i = 0; i < SENSOR_NUM; i++) {
        printf("  sensor[%d] 白 = %u、黒 = %u\n", i, (unsigned)line_cal_min(i), (unsigned)line_cal_max(i));
    }
}

static void test_battery(void)
{
#if PIN_VBAT < 0
    printf("この基板の設定（教育用の実験基板）では、電源電圧を測っていません。定格の %.1f V と見なしています。\n",
           (double)VBAT_NOMINAL_V);
    return;
#else
    printf("電源電圧（GP%d）。電池をつないでいれば、7〜8.4V くらいのはずです。何かキーを押すと戻ります。\n", PIN_VBAT);
#endif
    control_snapshot_t s;
    while (!wait_or_key(WATCH_INTERVAL_MS * 5)) {
        control_get_snapshot(&s);
        printf("電源電圧 = %.2f V\n", (double)s.vbat_v);
    }
}

static void test_encoder(void)
{
#if BOARD_EDU
    printf("【教育用の実験基板の設定】左エンコーダは、実験基板のロータリーエンコーダ（つまみ、GP%d・GP%d）です。\n",
           ENCODE_INPUT_PIN_L, ENCODE_INPUT_PIN_L + 1);
    printf("つまみを回すと、左の数が変わります。右エンコーダは、つないでいません。\n");
#endif
    printf("エンコーダ。車輪を手で、前進の向きに回してください。数が増えれば正しい向きです。\n");
    printf("数が減るときは、board.h の ENC_L_SIGN（左）か ENC_R_SIGN（右）の符号を逆にします。\n");
    /* タイヤ1回転で進む距離（2 × 3.1416 × 半径）に、1mあたりの数を掛けます。0.5 を足すのは、四捨五入のためです。 */
    printf("タイヤ1回転で、数は %d 増えるはずです。何かキーを押すと戻ります。\n",
           (int)(PULSE_COUNT_1M * 2.0f * PI * WHEEL_RADIUS + 0.5f));
    control_snapshot_t s;
    while (!wait_or_key(WATCH_INTERVAL_MS)) {
        control_get_snapshot(&s);
        printf("左 %8ld（%+.2f m/s）  右 %8ld（%+.2f m/s）\n", (long)s.enc_l, (double)s.speed_l, (long)s.enc_r,
               (double)s.speed_r);
    }
}

static void test_motor(bool left)
{
    const char *name = left ? "左" : "右";
    printf("%sのモーターを、duty +%d%% で %d ms 回します。\n", name, (int)JOG_DUTY, JOG_MS);
    printf("【注意】車輪が床や机に触れないように、機体を持ち上げてください。良ければ y を押します。\n");
    const int c = wait_key();
    if (c != 'y' && c != 'Y') {
        printf("やめました。\n");
        return;
    }

    control_snapshot_t before, after;
    control_get_snapshot(&before);
    if (left) {
        motor_set(JOG_DUTY, 0.0f);
    } else {
        motor_set(0.0f, JOG_DUTY);
    }
    sleep_ms(JOG_MS);
    motor_stop();
    sleep_ms(200);
    motor_free();
    control_get_snapshot(&after);

    const long d_l = (long)(after.enc_l - before.enc_l);
    const long d_r = (long)(after.enc_r - before.enc_r);
    printf("エンコーダの数の増え方：左 %+ld、右 %+ld\n", d_l, d_r);
    printf("正しい状態：%sの車輪が前進の向きに回り、%sの数だけが増える。\n", name, name);
    printf("  車輪が後退の向きに回った → board.h の MOTOR_%c_SIGN を逆にする\n", left ? 'L' : 'R');
    printf("  車輪は前進なのに数が減った → board.h の ENC_%c_SIGN を逆にする\n", left ? 'L' : 'R');
    printf("  反対側の数が増えた → エンコーダの左右の配線か、ENCODE_INPUT_PIN_L と ENCODE_INPUT_PIN_R（encoded_geared_motor.h）が逆\n");
    printf("  どちらも増えない → モーターの電源（電池）、配線、エンコーダの配線を確かめる\n");
}

static void test_both_motors(void)
{
    printf("両方のモーターを、duty +%d%% で 1000 ms 回します（前進の向き）。\n", (int)JOG_DUTY);
    printf("【注意】車輪が床や机に触れないように、機体を持ち上げてください。良ければ y を押します。\n");
    const int c = wait_key();
    if (c != 'y' && c != 'Y') {
        printf("やめました。\n");
        return;
    }
    control_snapshot_t before, mid, after;
    control_get_snapshot(&before);
    motor_set(JOG_DUTY, JOG_DUTY);
    sleep_ms(900);
    control_get_snapshot(&mid); /* 回っている最中の速さ */
    sleep_ms(100);
    motor_stop();
    sleep_ms(200);
    motor_free();
    control_get_snapshot(&after);
    printf("エンコーダの数の増え方：左 %+ld、右 %+ld / 回っている最中の速さ：左 %+.2f m/s、右 %+.2f m/s\n",
           (long)(after.enc_l - before.enc_l), (long)(after.enc_r - before.enc_r), (double)mid.speed_l,
           (double)mid.speed_r);
    printf("正しい状態：両方の車輪が前進の向きに回り、数も速さも、両方とも正で、左右がほぼ同じ。\n");
    printf("ここまで合っていれば、走行を試せます。合わないときは、6 と 7 で片方ずつ確かめます。\n");
}

static void test_gyro(void)
{
    const uint8_t id = gyro_who_am_i();
    printf("ジャイロ（担当の方のドライバ l3gd20h）。起動時に読んだ WHO_AM_I = %02X（L3GD20 は D4、L3GD20H は D7）\n", id);
    if (id != 0xD4u && id != 0xD7u) {
        printf(" → 返事が違います。配線（CS = GP%d）と、ICの型番を確かめてください。\n", PIN_GYRO_CS);
    }
    printf("機体を水平に回してください。左回り（上から見て反時計回り）で正になれば、正しい向きです。\n");
    printf("負になるときは、board.h の GYRO_Z_SIGN を逆にします。止めているときは、0 に近い値になります。\n");
    printf("何かキーを押すと戻ります。\n");
    control_set_gyro_monitor(true); /* USE_GYRO が 0 でも、表示している間だけ、割り込みに読んでもらう */
    control_snapshot_t s;
    while (!wait_or_key(WATCH_INTERVAL_MS)) {
        control_get_snapshot(&s);
        printf("Z軸の角速度 = %+8.1f deg/s\n", (double)s.gyro_dps);
    }
    control_set_gyro_monitor(false);
#if !USE_GYRO
    printf("（params.h の USE_GYRO が 0 なので、走行の制御には、この値を使いません。）\n");
#endif
}

static void flash_erase_all(void)
{
    printf("全消去を始めます。長いときは30秒かかります。終わるまで、電源を切らないでください。\n");
    control_set_busy_led(true);
    const absolute_time_t t0 = get_absolute_time();
    const AT25SF_result_t r = log_erase_all();
    control_set_busy_led(false);
    const long ms = (long)(absolute_time_diff_us(t0, get_absolute_time()) / 1000);
    if (r == AT25SF_OK) {
        printf("全消去が終わりました（%ld ms）。走行を5回まで記録できます。\n", ms);
    } else {
        printf("全消去に失敗しました（%ld ms） : %s\n", ms, AT25SF_result_name(r));
    }
}

/** ダミーの記録を、次の走行の場所に書いて、読み戻して比べます。走行の記録を1回分使います。 */
static void flash_write_and_verify(void)
{
    if (!log_start()) {
        printf("記録を始められませんでした。全消去がまだか、5回分を使い切っています。先に e で全消去をしてください。\n");
        return;
    }
    log_stats_t st;
    log_get_stats(&st);
    const int run = (int)st.run;
    printf("走行%d の場所（アドレス 0x%06lX から）に、ダミーの記録を %lu 件書きます（約1秒）。\n", run,
           (unsigned long)st.write_addr, (unsigned long)LOGTEST_RECORDS);

    control_logtest_start(LOGTEST_RECORDS);
    while (control_logtest_busy()) {
        flash_task();
    }
    const bool stopped = log_stop();

    uint32_t bad = 0;
    for (uint32_t i = 0; i < LOGTEST_RECORDS; i++) {
        Sensors_data_t rec;
        if (!log_read_record(run, i, &rec) || rec.vcc_mv != (uint16_t)i || rec.sensor_displacement != -15 ||
            rec.sensor_binary != 0xF0 || rec.target_speed != 1234 || rec.left_speed != -255 || rec.right_speed != 255 ||
            rec.left_motor_duty != 15 || rec.right_motor_duty != -16 || rec.target_angular_velocity != -256 ||
            rec.now_angular_velocity != 255) {
            if (bad < 5) {
                printf("  %lu 件目が合いません\n", (unsigned long)i);
            }
            bad++;
        }
    }
    log_get_stats(&st);
    const uint32_t counted = log_run_records(run);
    printf("最後のページまで書けたか : %s\n", stopped ? "OK" : "NG");
    printf("フラッシュから数え直した件数 : %lu 件（%lu 件が正常）\n", (unsigned long)counted, (unsigned long)LOGTEST_RECORDS);
    printf("合わなかった件数 : %lu 件（0が正常）\n", (unsigned long)bad);
    printf("捨てたページ数 : %lu、書き込みエラーの回数 : %lu、ページを送り直した回数 : %lu（どれも0が正常）\n",
           (unsigned long)st.dropped_pages, (unsigned long)st.write_errors, (unsigned long)st.write_retries);
    printf("1ページをICへ送る時間 : 最大 %lu us\n", (unsigned long)st.max_send_us);
    const bool pass = stopped && bad == 0 && counted == LOGTEST_RECORDS && st.dropped_pages == 0 && st.write_errors == 0;
    printf("結果 : %s\n", pass ? "合格" : "不合格");
    printf("※この確認で、走行%d の場所を使いました。残りは %u 回です。全消去をすると、また5回分使えます。\n", run,
           (unsigned)(LOG_RUN_NUM - st.used_runs));
}

static void test_flash(void)
{
    uint8_t id[3];
    AT25SF_read_id(id);
    printf("フラッシュメモリ。JEDEC ID = %02X %02X %02X（AT25SF321B なら 1F 87 01）、SR1 = %02X、SPI = %lu Hz\n", id[0],
           id[1], id[2], AT25SF_read_status1(), (unsigned long)AT25SF_get_baudrate());
    if (AT25SF_check_id() != AT25SF_OK) {
        printf(" → IDが違います。配線（SCK=GP2、MOSI=GP3、MISO=GP4、CS=GP5）と電源を確かめてください。\n");
        return;
    }
    log_stats_t st;
    log_get_stats(&st);
    if (st.formatted) {
        printf("全消去 : 済み / 記録が入っている走行の数 : %lu / 次に記録する走行 : %d（0 は「5回分を使い切った」）\n",
               (unsigned long)st.used_runs, log_next_run());
    } else {
        printf("全消去 : まだ（このままでは記録できません）\n");
    }
    printf(" e : 全消去をする（長いときは30秒。5回分の記録が全部消えます）\n");
    printf(" y : ダミーの記録を書いて、読み戻して比べる（走行の記録を1回分使います）\n");
    printf(" そのほかのキー : 戻る\n");
    const int c = wait_key();
    if (c == 'e' || c == 'E') {
        flash_erase_all();
    } else if (c == 'y' || c == 'Y') {
        flash_write_and_verify();
    } else {
        printf("戻ります。\n");
    }
}

static void print_menu(void)
{
    printf("\n=== 基板の動作確認（上から順に確かめます） ===\n");
    printf(" 1 : スイッチとLED\n");
    printf(" 2 : ブザー\n");
    printf(" 3 : ラインセンサーとマーカーセンサー\n");
    printf(" 4 : 電源電圧\n");
    printf(" 5 : エンコーダ（車輪を手で回す）\n");
    printf(" 6 : 左モーター（機体を持ち上げてから）\n");
    printf(" 7 : 右モーター（機体を持ち上げてから）\n");
    printf(" b : 両方のモーターを前進の向きに（機体を持ち上げてから）\n");
    printf(" 8 : ジャイロ\n");
    printf(" 9 : フラッシュメモリ\n");
    printf(" q : 戻る\n");
    printf(" ※ スイッチでの操作（走行、キャリブレーション、全消去）は、q で戻ってから行います。\n");
}

void selftest_menu(void)
{
    print_menu();
    for (;;) {
        flash_task();
        const int c = key_now();
        if (c < 0) {
            continue;
        }
        switch (c) {
        case '1': test_switch_led();  break;
        case '2': test_buzzer();      break;
        case '3': test_line_sensor(); break;
        case '4': test_battery();     break;
        case '5': test_encoder();     break;
        case '6': test_motor(true);   break;
        case '7': test_motor(false);  break;
        case 'b': test_both_motors(); break;
        case '8': test_gyro();        break;
        case '9': test_flash();       break;
        case 'q':
        case 'Q':
            motor_free();
            /* 確認の間に押したスイッチの「短押し」「長押し」を捨てます。
             * 捨てないと、メニューを出た直後に、走行や消去が始まってしまいます。
             */
            (void)sw_take_event(0);
            (void)sw_take_event(1);
            printf("動作確認を終わります。\n");
            return;
        case '\r':
        case '\n':
            continue;
        default:
            break;
        }
        print_menu();
    }
}
