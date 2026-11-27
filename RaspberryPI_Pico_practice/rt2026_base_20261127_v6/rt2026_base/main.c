/**
 * @file  main.c
 * @brief 2026年モデルのロボトレーサの、基礎プログラムです。
 *
 * ■ 操作
 *   スイッチ0を短く押す    : 走行開始（START_DELAY_MS 後に動き出す）
 *   スイッチ0を1秒長押し   : キャリブレーション（機体をラインの上で左右に動かす。
 *                            もう一度スイッチ0を押すか、CAL_MS が過ぎると終わる）
 *   スイッチ1を2秒長押し   : フラッシュメモリの全消去（長いときは30秒。始めと終わりに、長いブザー）
 *   走行中にどちらかを押す : 停止
 *   USBシリアル（待ちの間）: 1〜5 = その走行の記録をCSVで出す、d = いちばん新しい走行の記録を出す、
 *                            s = 状態を表示、h = 基板の動作確認、E = 全消去、? = 操作の説明
 *
 * ■ 記録
 *   1回の全消去で、走行を5回まで記録できます（走行1は64.5秒、走行2〜5は48.9秒まで）。
 *   全消去がまだのときと、5回分を使い切った後は、記録なしで走ります。
 *
 * ■ 分担
 *   このファイル（main）      : 操作の受け付け、フラッシュメモリの消去・書き込み・読み出し
 *   control.c（1msの割り込み）: センサー、走行の制御、log_put()
 *
 * ■ 調整する所
 *   ピンと向き → board.h    走りの値 → params.h
 */
#include <stdio.h>

#include "at25sf.h"
#include "board.h"
#include "buzzer/buzzer.h"
#include "control.h"
#include "encoded_geared_motor/encoded_geared_motor.h"
#include "flash_log.h"
#include "gyro/gyro.h"
#include "hardware/watchdog.h"
#include "line_sensor.h"
#include "params.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "selftest.h"
#include "sw/sw.h"

static repeating_timer_t control_timer; /* タイマーが動いている間ずっと必要なので、関数の外に置く */
static bool flash_ok = false;           /* フラッシュメモリと通信できているか                   */
static bool recording = false;          /* 今の走行を記録しているか                             */

static const char *stop_reason_name(stop_reason_t r)
{
    switch (r) {
    case STOP_GOAL:      return "ゴールのマーカーで止まった";
    case STOP_SWITCH:    return "スイッチで止めた";
    case STOP_LINE_LOST: return "ラインを見失って止まった";
    case STOP_TIMEOUT:   return "時間切れで止まった";
    case STOP_ENCODER:   return "duty を出しているのに、車輪が回っていないか、逆に回っているので止まった";
    default:             return "不明";
    }
}

/** スイッチにたまっている「短押し」「長押し」を捨てます。 */
static void flush_sw_events(void)
{
    (void)sw_take_event(0);
    (void)sw_take_event(1);
}

static void print_help(void)
{
    printf("\n--- 操作 ---\n");
    printf("スイッチ0 短く押す : 走行開始 / 1秒長押し : キャリブレーション（もう一度押すと終わり）\n");
    printf("スイッチ1 2秒長押し : フラッシュメモリの全消去（長いときは30秒）\n");
    printf("キー 1〜5 : その走行の記録をCSVで出す / d : いちばん新しい走行の記録を出す\n");
    printf("キー s : 状態を表示 / h : 基板の動作確認 / E : 全消去 / ? : この説明\n");
}

/** 1msの割り込みにかかった時間を表示します。表示すると、最大は、そこから測り直しになります。 */
static void print_isr_time(const char *period)
{
    control_isr_time_t it;
    control_take_isr_time(&it);
    printf("1msの割り込みにかかった時間 : 直前の1秒の平均 %lu us / %sの最大 %lu us / 1000 us を超えた回数 %lu（0が正常）\n",
           (unsigned long)it.avg_us, period, (unsigned long)it.max_us, (unsigned long)it.over_count);
    if (it.over_count != 0 || it.max_us > 800u) {
        printf(" → 割り込みの間隔（1000 us）に対して、余裕が少ないです。README の「うまくいかないとき」を見てください。\n");
    }
}

/** ジャイロの今の値を表示します。after_run が true のときは、走行の後の表示にします。 */
static void print_gyro(const control_snapshot_t *s, bool after_run)
{
    if (!gyro_is_present()) {
        if (!after_run) {
            printf("ジャイロ : 返事がありません（WHO_AM_I = %02X）。記録の角速度は、車輪の速さの差から計算した値になります。\n",
                   gyro_who_am_i());
        }
        return;
    }
    if (after_run) {
        printf("ジャイロ : この走行の前に測ったオフセット %+.2f deg/s（測っている間の振れ幅 %.2f deg/s）/ 走行中に回った角度 %+.1f 度（左回りが正）\n",
               (double)s->gyro_offset_dps, (double)s->gyro_spread_dps, (double)s->gyro_angle_deg);
    } else {
        printf("ジャイロ : 角速度 %+.2f deg/s（オフセット %+.2f を引いた値。測っている間の振れ幅 %.2f）/ 制御に使う設定 : %s\n",
               (double)s->gyro_dps, (double)s->gyro_offset_dps, (double)s->gyro_spread_dps, USE_GYRO ? "使う" : "使わない（記録だけ）");
    }
    if (s->gyro_spread_dps > GYRO_SPREAD_WARN_DPS) {
        printf(" → オフセットを測っている間に、機体が動いたようです（振れ幅が %.1f deg/s より大きい）。スイッチを押した後の1秒は、機体に触らないでください。\n",
               (double)GYRO_SPREAD_WARN_DPS);
    }
}

static void print_status(void)
{
    control_snapshot_t s;
    log_stats_t st;
    control_get_snapshot(&s);
    log_get_stats(&st);

    printf("\n--- 状態 ---\n");
#if PIN_VBAT >= 0
    printf("電源電圧 %.2f V / キャリブレーション %s / フラッシュメモリ %s\n", (double)s.vbat_v,
           line_cal_done() ? "済み" : "まだ", flash_ok ? "通信OK" : "通信NG");
#else
    printf("電源電圧 測っていません（この基板の設定では、定格の %.1f V と見なしています） / キャリブレーション %s / フラッシュメモリ %s\n",
           (double)VBAT_NOMINAL_V, line_cal_done() ? "済み" : "まだ", flash_ok ? "通信OK" : "通信NG");
#endif
    print_isr_time("前に表示してから");
    print_gyro(&s, false);
    if (!flash_ok) {
        return;
    }
    if (!st.formatted) {
        printf("記録 : 全消去がまだです（スイッチ1を2秒長押し、またはキー E）。今は、記録なしで走ります。\n");
        return;
    }
    const int next = log_next_run();
    if (next != 0) {
        printf("記録 : 次の走行は「走行%d」として記録します（残り %u 回）。\n", next, (unsigned)(LOG_RUN_NUM - st.used_runs));
    } else {
        printf("記録 : 5回分を使い切りました。記録するには、読み出した後で全消去をしてください。\n");
    }
    for (int run = 1; run <= (int)LOG_RUN_NUM; run++) {
        const uint32_t n = log_run_records(run);
        printf("  走行%d : %5lu 件（%6.3f 秒） / 上限 %lu 件\n", run, (unsigned long)n, (double)n / 1000.0,
               (unsigned long)log_run_capacity(run));
    }
}

/** フラッシュメモリの全体を消去します。長いときは30秒かかります。その間も、1msの割り込みは動いています。 */
static void do_erase(void)
{
    if (!flash_ok) {
        printf("フラッシュメモリと通信できていないので、消去できません。\n");
        setBeepPattern(BUZZER_PIN, 0xFF00u, DO_6);
        return;
    }
    printf("全消去を始めます。長いときは30秒かかります。終わりのブザーが鳴るまで、電源を切らないでください。\n");
    setBeepPattern(BUZZER_PIN, 0xFFFFu, SO_6); /* 始まりの合図：長く */
    control_set_busy_led(true);
    const absolute_time_t t0 = get_absolute_time();
    const AT25SF_result_t r = log_erase_all();
    control_set_busy_led(false);
    const long ms = (long)(absolute_time_diff_us(t0, get_absolute_time()) / 1000);
    if (r == AT25SF_OK) {
        printf("全消去が終わりました（%ld ms）。走行を5回まで記録できます。\n", ms);
        setBeepPattern(BUZZER_PIN, 0xFFFFu, DO_7); /* 終わりの合図：長く、高い音 */
    } else {
        printf("全消去に失敗しました（%ld ms） : %s\n", ms, AT25SF_result_name(r));
        setBeepPattern(BUZZER_PIN, 0xAAAAu, DO_6); /* 失敗：低い音で刻む */
    }
    flush_sw_events(); /* 消去の間に押されたスイッチは、数えない */
}

/** 走行を始めます。記録できる状態なら記録し、できなければ記録なしで走ります。 */
static void do_start_run(void)
{
    if (!line_cal_done()) {
        printf("キャリブレーションがまだです。スイッチ0を1秒長押ししてください。\n");
        setBeepPattern(BUZZER_PIN, 0xAA00u, DO_6);
        return;
    }
    /* 電池が減っているときは、走りません（リチウムポリマー電池は、使い切ると傷みます）。 */
    control_snapshot_t s;
    control_get_snapshot(&s);
    if (s.vbat_v > VBAT_PRESENT_V && s.vbat_v < VBAT_LOW_V) {
        printf("電池の電圧が低いので、走行を始めません（%.2f V。%.1f V より低い）。充電してください。\n", (double)s.vbat_v,
               (double)VBAT_LOW_V);
        printf(" → テスターで測った電圧と大きく違うときは、board.h の VBAT_R_TOP と VBAT_R_BOTTOM（分圧の抵抗の値）を確かめてください。\n");
        setBeepPattern(BUZZER_PIN, 0xAAA0u, DO_6);
        return;
    }
    if (s.vbat_v <= VBAT_PRESENT_V) {
        printf("【注意】電源電圧が %.2f V と読めています。電池をつないでいないと、モーターは回りません。\n", (double)s.vbat_v);
    }

    recording = flash_ok && log_start();
    if (recording) {
        log_stats_t st;
        log_get_stats(&st);
        printf("走行を始めます（走行%lu として記録します）。\n", (unsigned long)st.run);
    } else if (!flash_ok) {
        printf("走行を始めます（記録なし。フラッシュメモリと通信できていません）。\n");
    } else if (log_next_run() == 0) {
        log_stats_t st;
        log_get_stats(&st);
        printf("走行を始めます（記録なし。%s）。\n",
               st.formatted ? "5回分を使い切りました。読み出した後で、全消去をしてください" : "全消去がまだです");
    } else {
        printf("走行を始めます（記録なし。記録を始められませんでした）。\n");
    }
    {
        control_isr_time_t discard; /* 走行の間の最大を測るために、ここで測り直しにします */
        control_take_isr_time(&discard);
    }
    control_start_run(recording);
}

/** 走行が終わった後の片付け。記録を閉じ、結果を表示し、ブザーで知らせます。 */
static void finish_run(void)
{
    const stop_reason_t why = control_stop_reason();
    printf("走行が終わりました : %s\n", stop_reason_name(why));
    print_isr_time("この走行の間");
    {
        control_snapshot_t gs;
        control_get_snapshot(&gs);
        print_gyro(&gs, true);
    }
    if (why == STOP_ENCODER) {
        printf(" → 電池がつながっているかを確かめてください。つながっているなら、動作確認メニュー（h）の 5、6、7、b で、\n");
        printf("    エンコーダとモーターの向き（board.h）と配線を確かめてください。\n");
    }

    bool log_good = true;
    if (recording) {
        const bool stopped = log_stop();
        log_stats_t st;
        log_get_stats(&st);
        log_good = stopped && st.dropped_pages == 0 && st.write_errors == 0;
        printf("記録 : 走行%lu に %lu 件（%.3f 秒）/ 捨てたページ数 %lu / 書き込みエラーの回数 %lu / ページを送り直した回数 %lu → %s\n",
               (unsigned long)st.run, (unsigned long)st.records, (double)st.records / 1000.0,
               (unsigned long)st.dropped_pages, (unsigned long)st.write_errors, (unsigned long)st.write_retries,
               log_good ? "使えます" : "欠けがあります");
        if (st.full) {
            printf(" → 走行%lu の場所がいっぱいになったので、途中で記録を止めました。\n", (unsigned long)st.run);
        }
        printf("残りの記録できる回数 : %u 回。読み出すには、キー %lu を押します。\n", (unsigned)(LOG_RUN_NUM - st.used_runs),
               (unsigned long)st.run);
        recording = false;
    }
    /* 記録が使えるなら高い音で短く2回、欠けがあれば低い音で長く。 */
    setBeepPattern(BUZZER_PIN, log_good ? 0xA000u : 0xFFF0u, log_good ? DO_7 : DO_6);

    /* 止めるために押したスイッチが離れるのを待ってから、たまった出来事を捨てます。 */
    while (sw_is_pressed(0) || sw_is_pressed(1)) {
        flash_task();
    }
    flush_sw_events();

    /* 割り込みに「片付けが終わった」と伝え、待ちの状態に戻るのを確かめてから戻ります。
     * 確かめずに戻ると、割り込みが次に動くまでの間（1ms以内）、main のループが
     * 「まだ STATE_DONE だ」と見て、この関数をもう一度呼んでしまいます。
     */
    control_ack_done();
    while (control_state() == STATE_DONE) {
        tight_loop_contents();
    }
}

/** キャリブレーションが終わっていたら、結果を表示します。 */
static void report_calibration(void)
{
    const cal_result_t r = control_take_cal_result();
    if (r == CAL_RESULT_OK) {
        printf("キャリブレーションが終わりました : 成功\n");
    } else if (r == CAL_RESULT_NG) {
        printf("キャリブレーションが終わりました : 失敗（前の値に戻しました）。白と黒の差が足りなかったセンサー :");
        const uint8_t bits = line_cal_fail_bits();
        for (int i = 0; i < SENSOR_NUM; i++) {
            if (bits & (1u << i)) {
                printf(" sensor[%d]", i);
            }
        }
        printf("\n → 8個のセンサー全部が、白いラインと黒い床の両方を通るように、機体を左右に大きく動かしてください。\n");
    }
}

/** 記録を1回分、CSVで出します。run が 0 のときは、いちばん新しい走行を出します。 */
static void do_dump(int run)
{
    if (!flash_ok) {
        printf("フラッシュメモリと通信できていません。\n");
        return;
    }
    if (run == 0) {
        log_stats_t st;
        log_get_stats(&st);
        run = (int)st.used_runs;
        if (run == 0) {
            printf("記録が入っている走行がありません。\n");
            return;
        }
    }
    log_dump(run);
    flush_sw_events(); /* 読み出しの間に押されたスイッチは、数えない */
}

int main(void)
{
    /* 今回の起動が、見張りのタイマー（ウォッチドッグ）によるリセットの後かどうか。下で表示します。 */
    const bool wd_reboot = watchdog_enable_caused_reboot();

    stdio_init_all();

    /* 先に、モーターを止めた状態にします。motor_driver_init() は、モーターのPWMとエンコーダの両方を設定します
     * （担当の方の encoded_geared_motor）。
     */
    motor_driver_init();
    motor_free();

    control_init();
    sw_init();
    initBeep();
    line_sensor_init(); /* A/D変換IC（SPI1）。ジャイロより先に呼ぶ */
    const bool gyro_ok = gyro_init();

    const AT25SF_result_t fr = log_init();
    flash_ok = (fr == AT25SF_OK);

    /* 1msの割り込みを始めます。-1 は「前回の開始から1ms後」という意味です。 */
    const bool timer_ok = add_repeating_timer_ms(-1, control_timer_cb, NULL, &control_timer);

#if USE_WATCHDOG
    /* 見張りのタイマーを始めます。1msの割り込みが長く止まると、マイコンがリセットされ、モーターが止まります
     * （割り込みの側の処理は、control.c の control_timer_cb の最後にあります）。
     * true は「デバッガで止めている間は数えない」です。
     * 前回のリセットがこのタイマーによるものだったときは、始めません。原因が残っていた場合に、
     * リセットを繰り返して何も操作できなくなるのを防ぐためです。
     */
    if (timer_ok && !wd_reboot) {
        watchdog_enable(WATCHDOG_MS, true);
    }
#endif

    /* 起動の合図：フラッシュと通信できていれば高い音で短く2回、できていなければ低い音で長く。
     * 見張りのタイマーによるリセットの後は、低い音で刻みます。
     */
    if (wd_reboot) {
        setBeepPattern(BUZZER_PIN, 0xAAAAu, DO_6);
    } else {
        setBeepPattern(BUZZER_PIN, flash_ok ? 0xA000u : 0xFF00u, flash_ok ? DO_7 : DO_6);
    }

    /* 起動のメッセージは、PCのシリアルの画面がつながった時に出します（下のループの中）。
     * 電源を入れた直後に出すと、画面を開く前に流れてしまうためです。
     */
    bool usb_was_connected = false;

    for (;;) {
        /* 書き込みを待っているページがあれば、フラッシュに送ります。毎回呼びます。 */
        flash_task();

        const run_state_t st = control_state();

        /* シリアルの画面が（新しく）つながったら、起動のメッセージと状態を出します。走行中は出しません。 */
        const bool usb_connected = stdio_usb_connected();
        if (usb_connected && !usb_was_connected && st == STATE_WAIT) {
            sleep_ms(300); /* つながった直後の文字は、PCの側で落ちることがあるので、少し待ちます */
            printf("\n=== RT2026 基礎プログラム ===\n");
#if BOARD_EDU
            printf("基板の設定 : 教育用の実験基板とブレッドボード（board.h の BOARD_EDU が 1）\n");
#else
            printf("基板の設定 : 機体の基板（board.h の BOARD_EDU が 0）\n");
#endif
#if defined(PICO_RP2040) && PICO_RP2040
            printf("マイコン   : Pico（RP2040）向けのビルド\n");
#else
            printf("マイコン   : Pico 2（RP2350）向けのビルド\n");
#endif
            printf("フラッシュメモリ : %s / ジャイロ : %s（WHO_AM_I = %02X。L3GD20 は D4、L3GD20H は D7）\n",
                   flash_ok ? "通信OK" : AT25SF_result_name(fr), gyro_ok ? "返事あり" : "返事なし", gyro_who_am_i());
            if (!timer_ok) {
                printf("1msのタイマーを始められませんでした。スイッチも走行も動きません。\n");
            }
            if (wd_reboot) {
                printf("【注意】前回は、1msの割り込みが長く止まったので、見張りのタイマー（ウォッチドッグ）が\n");
                printf("        マイコンをリセットしました。プログラムが途中で止まった印です。直前に変えた所を確かめてください。\n");
                printf("        今回は、見張りのタイマーを使わずに動かしています。\n");
                printf("        リセットのスイッチを押すか、電源を入れ直すと、また使います。\n");
            }
            print_status();
            print_help();
        }
        if (st == STATE_WAIT || !usb_connected) {
            usb_was_connected = usb_connected;
        }
        if (st == STATE_DONE) {
            finish_run();
            continue;
        }
        if (st != STATE_WAIT) {
            /* キャリブレーション中と走行中は、操作を受け付けません（停止は、割り込みが見ています）。
             * その間に押されたスイッチは、後で働かないように、ここで捨てます。
             */
            flush_sw_events();
            continue;
        }
        report_calibration();

        /* ---- スイッチ ---- */
        const sw_event_t e0 = sw_take_event(0);
        const sw_event_t e1 = sw_take_event(1);
        if (e0 == SW_EV_SHORT) {
            do_start_run();
        } else if (e0 == SW_EV_LONG) {
            printf("キャリブレーションを始めます。機体をラインの上で左右に動かしてください。\n");
            printf("終わるには、スイッチ0をもう一度押します（押さなくても、%d ms で終わります）。\n", CAL_MS);
            control_start_calibration();
        } else if (e1 == SW_EV_LONG) {
            do_erase();
        }

        /* ---- USBシリアル ---- */
        const int c = getchar_timeout_us(0);
        switch (c) {
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
            do_dump(c - '0');
            break;
        case 'd':
            do_dump(0);
            break;
        case 'E':
            printf("全消去をします。5回分の記録が全部消えます。良ければ y を押してください。\n");
            {
                int k;
                do {
                    k = getchar_timeout_us(100000);
                } while (k == PICO_ERROR_TIMEOUT);
                if (k == 'y' || k == 'Y') {
                    do_erase();
                } else {
                    printf("やめました。\n");
                    flush_sw_events();
                }
            }
            break;
        case 'h':
            selftest_menu();
            print_help();
            break;
        case 's':
            print_status();
            break;
        case '?':
            print_help();
            break;
        default:
            break;
        }
    }
}
