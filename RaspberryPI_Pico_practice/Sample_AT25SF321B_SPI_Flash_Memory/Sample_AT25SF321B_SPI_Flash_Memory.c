/**
 * @file  main.c
 * @brief フラッシュのログを、机の上で試すためのプログラム 【アプリの層】
 *
 * この層の役割は、「いつ、何をするか」を決めることです。
 * スイッチの代わりに、PCから送る1文字のコマンドで切り替えます。
 * こうすると、スイッチ用のピンを使わずに済み、機体に載せたときにピンが余っているかを
 * 気にしなくて済みます。機体に載せるときは、このファイルだけを自分のプログラムに置き換えます
 * （README の「自分のプログラムに組み込む」を見てください）。
 *
 * ■ コマンド（シリアルモニタから1文字送る）
 *     h : コマンドの一覧
 *     i : JEDEC ID とステータスを表示する
 *     t : 自己診断（消す → 読む → 書く → 読み戻す）。最初に、これが通るかを確かめる
 *     e : ログの範囲を消去する（記録の前に、必ず行う）
 *     l : 1msごとのダミーの記録を 1000件 行う
 *     v : 記録を読み戻して、書いた値と同じかを自動で照合する
 *     d : 記録をCSVで出す
 *     s : 状態を表示する（書いたページ数、捨てたページ数、かかった時間）
 *     r : 先頭の64バイトを、16進でそのまま表示する
 *     U : 保護ビットを解除する（自己診断で保護が原因と出たときだけ。大文字）
 *     C : チップ全体を消去する（10〜30秒かかる。大文字）
 *   元に戻せない操作（U と C）は、押し間違えないように大文字にしてあります。
 *
 * ■ 最初に試す順番
 *     i → t → e → l → v → d
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "at25sf.h"
#include "flash_log.h"

/* 1000件 = 1秒分。16件で1ページなので、62ページと8件になります。
 * わざと半端な数にして、最後の「詰めかけのページ」を書く処理（log_stop）も確かめます。
 */
#define TEST_RECORDS     1000u
#define TEST_ERASE_BYTES AT25SF_BLOCK_SIZE /* 64KB = 約4秒分。消去は、データシートの標準で約0.3秒 */
/* 11/16の実機では、何も書いていない所の消去は、ずっと短く終わりました（4KBで約0.6ms、
 * 64KB+4KBで約8ms）。書いてある所の4KBの消去は、約27msでした。
 */

/* 自己診断で使う場所。ログ（先頭から使う）と重ならないように、いちばん最後の4KBを使います。 */
#define SELFTEST_ADDR (AT25SF_TOTAL_SIZE - AT25SF_SECTOR_SIZE)

/* 試験で書く、固定のダミー値。上位と下位のバイトが違う値、負の数を混ぜてあり、
 * バイトの並べ方や符号の扱いが間違っていれば、読み戻したときに分かります。
 * （0xF0F0 のように上位と下位が同じ値だと、2つのバイトが入れ違っていても気づけません）
 * vcc_mv だけは、記録のたびに連番（0, 1, 2, …）を入れます。読み戻したときに、
 * 抜けや重なりが無いかを確かめられるようにするためです。
 * （乱数だと、読み戻した値が正しいかどうかを比べられません）
 */
static const Sensors_data_t test_pattern = {
    .sensor_displacement = 0x0F,
    .sensor_binary = 0xF0,
    .target_speed = 0x1234,
    .left_speed = 0x00FF,
    .right_speed = 0xFF00,
    .left_motor_duty = 15,
    .right_motor_duty = -16,
    .target_angular_velocity = -256,
    .now_angular_velocity = 255,
    .vcc_mv = 0, /* 連番で上書きする */
};

/* ===== 1msタイマー（試験用） ================================================
 * 機体では、制御の割り込みがこの役をします。ここでは、タイマーで代用しています。
 */
static repeating_timer_t test_timer; /* タイマーが動いている間ずっと必要なので、関数の外に置く */
static volatile uint32_t test_count = 0;
static volatile bool     test_running = false;
static volatile bool     test_finished = false; /* 割り込み → main への合図 */

static bool test_timer_cb(repeating_timer_t *t)
{
    (void)t; /* 使わない引数。警告を出さないための書き方 */

    now_sensors_data.vcc_mv = (uint16_t)test_count; /* 連番 */
    log_put(&now_sensors_data);
    test_count++;

    if (test_count >= TEST_RECORDS) {
        /* 割り込みの中では、止める処理（SPIを使う）を行いません。
         * 合図だけ立てて、mainに任せます。false を返すと、タイマーが止まります。
         */
        test_finished = true;
        return false;
    }
    return true;
}

static void test_log_start(void)
{
    if (test_running) {
        printf("記録中です。\n");
        return;
    }
    now_sensors_data = test_pattern;

    if (!log_start()) {
        printf("記録を始められません。先に 'e' で消去してください（1回の消去につき、記録は1回です）。\n");
        return;
    }
    test_count = 0;
    test_finished = false;
    test_running = true;
    /* -1 は「前回の開始から1ms後」という意味です。正の値にすると
     * 「前回の終了から1ms後」になり、処理時間の分だけ周期が延びます。
     */
    if (!add_repeating_timer_ms(-1, test_timer_cb, NULL, &test_timer)) {
        test_running = false;
        log_stop();
        printf("タイマーを始められませんでした。\n");
        return;
    }
    printf("記録を始めました（%lu件、約1秒）。\n", (unsigned long)TEST_RECORDS);
}

/* ===== 表示 ================================================================= */

static void print_help(void)
{
    printf("\n--- コマンド ---\n");
    printf(" i : IDとステータス     t : 自己診断\n");
    printf(" e : ログの範囲を消去   l : 記録(%lu件)   v : 照合   d : CSVで出す\n",
           (unsigned long)TEST_RECORDS);
    printf(" s : 状態               r : 先頭64バイト(16進)\n");
    printf(" U : 保護の解除         C : チップ全消去\n");
    printf(" 最初は i → t → e → l → v → d の順に試してください。\n\n");
}

/** IDとステータスを表示します。IDが正しければ true。 */
static bool print_id_status(void)
{
    uint8_t id[3];
    AT25SF_read_id(id);
    uint8_t s1 = AT25SF_read_status1();
    uint8_t s2 = AT25SF_read_status2();
    bool id_ok = (id[0] == AT25SF_ID_BYTE0 && id[1] == AT25SF_ID_BYTE1 && id[2] == AT25SF_ID_BYTE2);

    printf("JEDEC ID : %02X %02X %02X （AT25SF321B なら 1F 87 01）\n", id[0], id[1], id[2]);
    printf("SPI      : %lu Hz\n", (unsigned long)AT25SF_get_baudrate());
    printf("SR1      : %02X  BUSY=%d WEL=%d BP=%02X SRP0=%d\n", s1,
           (s1 & AT25SF_SR1_BUSY) ? 1 : 0, (s1 & AT25SF_SR1_WEL) ? 1 : 0,
           (unsigned)(s1 & AT25SF_SR1_BP_MASK), (s1 & AT25SF_SR1_SRP0) ? 1 : 0);
    printf("SR2      : %02X\n", s2);

    if (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) {
        /* SO にはPico側のプルアップを入れてあるので、ICが何も出していないと FF になります。 */
        printf(" → 全部FFです。ICから返事がありません。\n");
        printf("   配線（CS、SCK、SI、SO）、ICの電源、ピンの番号（at25sf.h）を確かめてください。\n");
    } else if (id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00) {
        printf(" → 全部00です。SO の線がGNDに触れていないか、ピンの番号（at25sf.h）が合っているかを確かめてください。\n");
    } else if (!id_ok) {
        printf(" → IDが違います。返事はありますが、値が化けています。\n");
        printf("   SPIの速さ（AT25SF_SPI_BAUDRATE）を下げるか、配線を短くしてください。\n");
    }
    if (id_ok && (s1 & AT25SF_SR1_BP_MASK)) {
        printf(" → 保護ビットが立っています。この状態では消去も書き込みもできません。'U' で解除できます。\n");
    }
    return id_ok;
}

/** ドライバが数えている「つじつまの合わない読み取り」の回数を表示します。
 *  どちらも 0 が正常です。0 でなくても、ドライバは待ち直して先へ進みますが、
 *  配線やICの様子を調べる手がかりになるので、結果と一緒に出します。
 *  （11/16の実機テストで、消去が ERR_BUSY で止まったことを受けて足しました）
 */
static void print_diag(void)
{
    AT25SF_diag_t d;
    AT25SF_get_diag(&d);
    printf("読み直し       : 06hの後にBUSY %lu回", (unsigned long)d.wren_busy);
    if (d.wren_busy != 0) {
        printf("（SR1=%02X）", d.wren_busy_sr1);
    }
    printf("、BUSYの戻り %lu回", (unsigned long)d.busy_again);
    if (d.busy_again != 0) {
        printf("（SR1=%02X）", d.busy_again_sr1);
    }
    printf(" （0が正常。電源を入れてからの合計）\n");
    if (d.wren_busy != 0 || d.busy_again != 0) {
        printf(" → 0でない数があります。ドライバは待ち直して先へ進んでいますが、この行を控えておいてください。\n");
        printf("   SR1=FF : ICが答えていない / 03 : 前の処理がまだ続いていた / 00,02 : 途中で BUSY=0 と読めた\n");
    }
}

/** うまくいかなかったときに、次に確かめる所を表示します。 */
static void print_hint(AT25SF_result_t r)
{
    /* ステータスを見て判定したエラーのときは、そのとき読んだ値も出します。
     * FF なら「ICが答えていない」、03 なら「ICは本当に処理中」と、原因を分けられます。
     */
    if (r == AT25SF_ERR_BUSY || r == AT25SF_ERR_NO_WEL || r == AT25SF_ERR_PROTECTED ||
        r == AT25SF_ERR_NOT_STARTED || r == AT25SF_ERR_TIMEOUT) {
        AT25SF_diag_t d;
        AT25SF_get_diag(&d);
        printf(" → 判定に使った SR1 = %02X\n", d.error_sr1);
    }
    switch (r) {
    case AT25SF_OK:
        break;
    case AT25SF_ERR_ID:
        printf(" → ICと通信できていません。配線と電源を確かめ、'i' でIDを見てください。\n");
        break;
    case AT25SF_ERR_BUSY:
        printf(" → ICが「まだ処理中」と答えています（ドライバは、1秒まで待ち直しています）。\n");
        printf("   上の SR1 が FF なら、ICが答えていません。配線と、ICの電源を確かめてください。\n");
        break;
    case AT25SF_ERR_NO_WEL:
        printf(" → 06h（書き込み許可）が届いていないか、ステータスが読めていません。\n");
        printf("   SI、SO、SCK、CS の配線を確かめてください。\n");
        break;
    case AT25SF_ERR_PROTECTED:
        printf(" → 'U' で保護を解除してから、やり直してください。\n");
        break;
    case AT25SF_ERR_NOT_STARTED:
        printf(" → 命令が受け付けられていないか、ステータスが正しく読めていません。\n");
        printf("   SPIの速さ（AT25SF_SPI_BAUDRATE）を下げる、配線を短くする、\n");
        printf("   HOLD# と WP# を3.3Vにつなぐ、の順に試してください。\n");
        break;
    case AT25SF_ERR_TIMEOUT:
        printf(" → BUSYが外れません。SO の配線と、ICの電源を確かめてください。\n");
        break;
    case AT25SF_ERR_PARAM:
        printf(" → 引数（アドレスや長さ）が範囲の外です。\n");
        break;
    }
}

static void print_stats(void)
{
    log_stats_t st;
    log_get_stats(&st);
    printf("記録した件数   : %lu\n", (unsigned long)st.records);
    printf("書いたページ   : %lu\n", (unsigned long)st.pages_written);
    printf("捨てたページ   : %lu （0が正常）\n", (unsigned long)st.dropped_pages);
    printf("書き込みエラー : %lu （0が正常）\n", (unsigned long)st.write_errors);
    printf("送信の最大     : %lu us\n", (unsigned long)st.max_send_us);
    printf("BUSYの最大     : %lu us\n", (unsigned long)st.max_busy_us);
    printf("次のアドレス   : 0x%06lX / 上限 0x%06lX / 消去済み 0x%06lX\n",
           (unsigned long)st.write_addr, (unsigned long)st.capacity_bytes,
           (unsigned long)st.erased_until);
    printf("記録中=%d 満杯=%d\n", st.logging ? 1 : 0, st.full ? 1 : 0);
    print_diag();
}

static void print_hex(uint32_t addr, const uint8_t *p, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printf("%06lX :", (unsigned long)(addr + i));
        }
        printf(" %02X", p[i]);
        if (i % 16 == 15) {
            printf("\n");
        }
    }
}

/* ===== 自己診断 =============================================================
 * 消す → 読む → 書く → 読み戻す、を順に行い、どこで失敗したかを表示します。
 * 手順ごとに、確かめられることが1つずつあります。
 *   消去の後に読んで FF が並ぶ → 消去と読み出しが働いている
 *   書いた後に読み戻して一致   → 書き込みが働いている
 * 全消去（10〜30秒）ではなく4KBの消去（標準55ms）を使うので、何度でも試せます。
 */
static bool self_test(void)
{
    /* 256バイトの配列を2つ使います。関数の中に置くとスタックを使うので、static にしています。 */
    static uint8_t pattern[AT25SF_PAGE_SIZE];
    static uint8_t buf[AT25SF_PAGE_SIZE];
    AT25SF_result_t r;
    uint64_t t0;

    /* 書く値は 00, 01, 02, … FF。ログと同じ1ページ（256バイト）を、まるごと書いて確かめます。 */
    for (uint32_t i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (uint8_t)i;
    }

    printf("\n--- 自己診断（アドレス 0x%06lX の4KBを使います）---\n", (unsigned long)SELFTEST_ADDR);

    printf("[1] IDとステータス\n");
    if (!print_id_status()) {
        printf("    NG（IDが読めていないので、ここで止めます。IDが読めるようになってから、やり直してください）\n");
        return false;
    }
    printf("    OK\n");

    printf("[2] 書き込み許可: 06h を送り、WEL が 1 になるかを見る\n");
    AT25SF_write_enable();
    uint8_t s = AT25SF_read_status1();
    bool wel_ok = (s & (AT25SF_SR1_BUSY | AT25SF_SR1_WEL)) == AT25SF_SR1_WEL;
    printf("    SR1 = %02X → %s\n", s, wel_ok ? "OK" : "NG（06h が届いていないか、ステータスが正しく読めていません）");
    if (!wel_ok) {
        return false;
    }

    printf("[3] 4KBの消去\n");
    t0 = time_us_64();
    r = AT25SF_erase_sector(SELFTEST_ADDR);
    printf("    結果 = %s、時間 = %lu us（標準55ms、最大250ms。何も書いていない所は、1ms以下のこともあります）\n", AT25SF_result_name(r),
           (unsigned long)(time_us_64() - t0));
    if (r != AT25SF_OK) {
        print_hint(r);
        return false;
    }

    printf("[4] 消去の後に読む（256バイト全部が FF のはず。表示は先頭の16バイト）\n");
    /* 読む前に、バッファを0で埋めておきます。前の値が残っていると、
     * 読み出しが失敗していても、合っているように見えてしまうためです。
     */
    for (uint32_t i = 0; i < sizeof(buf); i++) {
        buf[i] = 0x00;
    }
    r = AT25SF_read(SELFTEST_ADDR, buf, sizeof(buf));
    print_hex(SELFTEST_ADDR, buf, 16);
    if (r != AT25SF_OK) {
        printf("    NG（読み出し : %s）\n", AT25SF_result_name(r));
        return false;
    }
    for (uint32_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0xFF) {
            printf("    NG（%lu バイト目が %02X で、FF になっていません）\n", (unsigned long)i, buf[i]);
            return false;
        }
    }
    printf("    OK\n");

    printf("[5] 1ページ（256バイト）書く\n");
    t0 = time_us_64();
    r = AT25SF_page_program(SELFTEST_ADDR, pattern, sizeof(pattern));
    printf("    結果 = %s、時間 = %lu us（送信の時間を含む。書き込み自体は標準0.4ms、最大3.4ms）\n",
           AT25SF_result_name(r), (unsigned long)(time_us_64() - t0));
    if (r != AT25SF_OK) {
        print_hint(r);
        return false;
    }

    printf("[6] 読み戻して比べる（表示は先頭の16バイト）\n");
    for (uint32_t i = 0; i < sizeof(buf); i++) {
        buf[i] = 0x00;
    }
    r = AT25SF_read(SELFTEST_ADDR, buf, sizeof(buf));
    print_hex(SELFTEST_ADDR, buf, 16);
    if (r != AT25SF_OK) {
        printf("    NG（読み出し : %s）\n", AT25SF_result_name(r));
        return false;
    }
    for (uint32_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != pattern[i]) {
            printf("    NG（%lu バイト目が違います。書いた値 %02X、読んだ値 %02X）\n", (unsigned long)i,
                   pattern[i], buf[i]);
            return false;
        }
    }
    printf("    OK\n");
    return true;
}

/* ===== 照合 =================================================================
 * 'l' で書いた記録を1件ずつ読み戻し、書いた値（固定のダミー値と連番）と比べます。
 * 1000行のCSVを目で追わなくても、抜けや重なり、値の化けが無いかが分かります。
 */
static void verify_test_log(void)
{
    uint32_t errors = 0;

    for (uint32_t i = 0; i < TEST_RECORDS; i++) {
        Sensors_data_t r;
        if (!log_read_record(i, &r)) {
            printf("NG : %lu 件目が読めません（書かれていないか、ICと通信できていません）。\n", (unsigned long)i);
            errors++;
            break;
        }
        bool same = r.sensor_displacement == test_pattern.sensor_displacement &&
                    r.sensor_binary == test_pattern.sensor_binary &&
                    r.target_speed == test_pattern.target_speed &&
                    r.left_speed == test_pattern.left_speed &&
                    r.right_speed == test_pattern.right_speed &&
                    r.left_motor_duty == test_pattern.left_motor_duty &&
                    r.right_motor_duty == test_pattern.right_motor_duty &&
                    r.target_angular_velocity == test_pattern.target_angular_velocity &&
                    r.now_angular_velocity == test_pattern.now_angular_velocity;
        if (!same || r.vcc_mv != (uint16_t)i) {
            if (errors < 5) { /* 表示が流れすぎないように、最初の5件だけ出す */
                printf("NG : %lu 件目  連番=%u（期待 %lu）、固定値=%s\n", (unsigned long)i,
                       (unsigned)r.vcc_mv, (unsigned long)i, same ? "一致" : "不一致");
            }
            errors++;
        }
    }

    /* 最後の記録の次は、書いていない（全部 FF の）はずです。 */
    Sensors_data_t next;
    if (errors == 0 && log_read_record(TEST_RECORDS, &next)) {
        printf("NG : %lu 件目に、余分な記録があります。\n", (unsigned long)TEST_RECORDS);
        errors++;
    }

    if (errors == 0) {
        printf("照合 : OK（%lu件、連番に抜けも重なりも無く、固定値もすべて一致）\n",
               (unsigned long)TEST_RECORDS);
    } else {
        printf("照合 : NG（合わなかった件数 %lu）。's' で、捨てたページが無いかを見てください。\n",
               (unsigned long)errors);
    }
}

/* ===== main ================================================================= */

int main(void)
{
    stdio_init_all();
    sleep_ms(2000); /* PCのシリアルモニタがつながるのを、少し待つ */

    AT25SF_result_t r = log_init();
    printf("\nAT25SF321B フラッシュログ 試験プログラム\n");
    printf("初期化 : %s\n", AT25SF_result_name(r));
    print_help();

    while (true) {
        /* PCから1文字来ているかを見ます。0 を指定すると、待たずにすぐ戻ります。
         * 何も来ていなければ、負の値（PICO_ERROR_TIMEOUT）が返ります。
         */
        int ch = getchar_timeout_us(0);

        /* 記録中は、コマンドを受け付けません。
         * コマンドの処理（長い表示や、SPIを使う読み出し）でmainのループが止まると、
         * その間 flash_task を呼べず、書き込みが間に合わなくなるためです。
         * 状態の表示（'s'）も、数百バイトの printf で数ms〜数十ms止まるので、同じ扱いにしています。
         */
        if (test_running && ch >= 0 && ch != '\r' && ch != '\n') {
            printf("記録中です。\n");
            ch = PICO_ERROR_TIMEOUT;
        }

        switch (ch) {
        case 'h':
            print_help();
            break;
        case 'i':
            print_id_status();
            break;
        case 't':
            printf("自己診断 : %s\n", self_test() ? "合格" : "不合格");
            print_diag();
            break;
        case 'e': {
            printf("消去します（%lu バイト分）...\n", (unsigned long)TEST_ERASE_BYTES);
            uint64_t t0 = time_us_64();
            AT25SF_result_t er = log_erase(TEST_ERASE_BYTES);
            printf("消去 : %s、時間 = %lu us\n", AT25SF_result_name(er), (unsigned long)(time_us_64() - t0));
            print_hint(er);
            print_diag();
            break;
        }
        case 'l':
            test_log_start();
            break;
        case 'v':
            verify_test_log();
            break;
        case 'd':
            log_dump();
            break;
        case 's':
            print_stats();
            break;
        case 'r': {
            static uint8_t head[64];
            AT25SF_result_t rr = AT25SF_read(0, head, sizeof(head));
            if (rr == AT25SF_OK) {
                print_hex(0, head, sizeof(head));
            } else {
                printf("読み出し : %s\n", AT25SF_result_name(rr));
                print_hint(rr);
            }
            break;
        }
        case 'U': {
            AT25SF_result_t ur = AT25SF_unprotect();
            printf("保護の解除 : %s\n", AT25SF_result_name(ur));
            print_hint(ur);
            print_id_status();
            break;
        }
        case 'C': {
            printf("チップ全体を消去します。10〜30秒かかります...\n");
            uint64_t t0 = time_us_64();
            AT25SF_result_t er = AT25SF_erase_chip();
            printf("全消去 : %s、時間 = %lu us\n", AT25SF_result_name(er), (unsigned long)(time_us_64() - t0));
            print_hint(er);
            print_diag();
            if (er == AT25SF_OK) {
                printf("ログを記録する前に、もう一度 'e' を送ってください。\n");
            }
            break;
        }
        default:
            break; /* 何も来ていない、または知らない文字 */
        }

        /* 札の付いたページがあれば、フラッシュに書きます。毎回呼びます。 */
        flash_task();

        /* タイマーからの合図を見て、記録を止めます。 */
        if (test_finished) {
            test_finished = false;
            bool ok = log_stop();
            test_running = false;
            printf("記録が終わりました : %s\n", ok ? "OK" : "NG");
            print_stats();
            printf("'v' で照合、'd' でCSVを出せます。\n");
        }
    }
}
