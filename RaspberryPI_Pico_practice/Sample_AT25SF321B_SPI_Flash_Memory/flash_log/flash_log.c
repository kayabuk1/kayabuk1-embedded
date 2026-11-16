/**
 * @file  flash_log.c
 * @brief 1msごとの記録を、フラッシュメモリにためるログ 【ログの層】
 *
 * ■ 箱詰め係と配達係
 *   割り込み（log_put）は「箱詰め係」です。1msごとに1件ずつ、箱（page_buf）に詰めます。
 *   main（flash_task）は「配達係」です。いっぱいになった箱を、倉庫（フラッシュIC）へ運びます。
 *   箱はAとBの2つです。いっぱいになった箱には「配達待ち」の札（buf_state）を付けます。
 *
 *     箱詰め係 : 箱がいっぱい → 札を付ける → もう一方の箱へ移る
 *     配達係   : 札を見つける → その箱を運ぶ → 札を外す
 *
 * ■ 変数ごとに「誰が書き、誰が読むか」
 *     変数            意味                              書く人                   読む人
 *     total_bytes     今どこに詰めているか              割り込み、main(停止後)   両方
 *     buf_state       どの箱が配達待ちか（札）          割り込み(付ける)         両方
 *                                                       main(外す)
 *     logging         記録中か                          main、割り込み(満杯)     両方
 *     log_full        上限まで使い切ったか              割り込み                 main
 *     dropped_pages   捨てたページ数                    割り込み                 main
 *     capacity_bytes  記録できる上限                    main(消去のとき)         両方
 *     flash_state     ICが書き込み中か                  main                     main
 *     write_addr      フラッシュの次の書き込み先        main                     main
 *     erased_until    消去が済んでいる範囲の終わり      main                     main
 *
 *   割り込みとmainの両方が触る変数には volatile を付けています。volatile が無いと、
 *   コンパイラが「この関数の中では変わらない」と判断して、割り込みでの変化を見落としたり、
 *   読む順番を入れ替えたりすることがあります。
 *
 * ■ SPIを使うのはmainだけ
 *   このファイルで、SPI（at25sf.h の関数）を使うのは main から呼ばれる関数だけです。
 *   log_put は、SPIに触りません。だから、mainがSPIで送っている途中に割り込みが入っても、
 *   送信が少し止まるだけで、通信は壊れません。
 */
#include "flash_log/flash_log.h"

#include <stdio.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico/stdlib.h"

/* 1ページの書き込みが、これ以上かかったら異常とみなします（データシートの最大は3.4ms）。 */
#define LOG_PAGE_TIMEOUT_US (20u * 1000u)
/* log_stop などで、残りを書き終えるのを待つ上限。 */
#define LOG_DRAIN_TIMEOUT_US (200u * 1000u)

/* ===== 状態 ================================================================= */

volatile Sensors_data_t now_sensors_data;

typedef enum {
    NOT_READY = -1, /* 配達待ちの箱は無い */
    BUF_A = 0,      /* 箱Aが配達待ち      */
    BUF_B = 1,      /* 箱Bが配達待ち      */
} buf_state_t;

typedef enum {
    FLASH_READY, /* ICは空いている。札の付いた箱があれば、送れる       */
    FLASH_BUSY,  /* 送ったページを、ICが中で書いている。終わるのを待つ */
} flash_state_t;

static uint8_t page_buf[2][AT25SF_PAGE_SIZE]; /* 箱AとB */

/* --- 割り込みとmainの両方が触る（volatile） --- */
static volatile buf_state_t buf_state = NOT_READY; /* 札 */
static volatile bool     logging = false;
static volatile bool     log_full = false;
static volatile uint32_t dropped_pages = 0;
static volatile uint32_t capacity_bytes = 0;

/* これまでに受け付けたバイト数の通算です。
 * ここから、どの箱の、どの位置に詰めるかを毎回計算します。
 *   箱の番号     = (total_bytes / 256) % 2    … 256バイトごとに A, B, A, B と入れ替わる
 *   箱の中の位置 = total_bytes % 256
 * フラッシュのアドレスを、ページ番号とページ内の位置に分けるのと同じ考え方です。
 * 箱を切り替えるための変数を別に持たないので、「切り替え忘れ」が起きません。
 */
static volatile uint32_t total_bytes = 0;

/* --- mainだけが使う --- */
static flash_state_t flash_state = FLASH_READY;
static bool     busy_is_page_write = false; /* 今のBUSYは、自分が送ったページの書き込みか */
static uint64_t busy_start_us = 0;
static uint32_t write_addr = 0;
static uint32_t erased_until = 0;
static uint32_t pages_written = 0;
static uint32_t write_errors = 0;
static uint32_t max_send_us = 0;
static uint32_t max_busy_us = 0;
static uint32_t pad_bytes = 0; /* log_stop で、最後のページを埋めるために足したバイト数 */

/* ===== 構造体 ⇔ 16バイト ====================================================
 * 構造体をそのまま memcpy せず、1バイトずつ「上位のバイト → 下位のバイト」の順に並べます。
 * こうしておくと、CPUのバイトの並び（エンディアン）や、コンパイラが入れる詰め物に
 * 左右されず、読み出す側も同じ順で戻せます。
 */
static void log_pack(uint8_t dst[LOG_RECORD_SIZE], const Sensors_data_t *src)
{
    dst[0] = src->sensor_displacement;
    dst[1] = src->sensor_binary;
    dst[2] = (uint8_t)(src->target_speed >> 8);
    dst[3] = (uint8_t)(src->target_speed);
    dst[4] = (uint8_t)(src->left_speed >> 8);
    dst[5] = (uint8_t)(src->left_speed);
    dst[6] = (uint8_t)(src->right_speed >> 8);
    dst[7] = (uint8_t)(src->right_speed);
    dst[8] = (uint8_t)src->left_motor_duty;
    dst[9] = (uint8_t)src->right_motor_duty;
    /* 符号付きの値は、先に符号なしにしてからシフトします。
     * 負の数の右シフトは、空いた所を0で埋めるか1で埋めるかが、コンパイラ任せだからです。
     */
    dst[10] = (uint8_t)((uint16_t)src->target_angular_velocity >> 8);
    dst[11] = (uint8_t)((uint16_t)src->target_angular_velocity);
    dst[12] = (uint8_t)((uint16_t)src->now_angular_velocity >> 8);
    dst[13] = (uint8_t)((uint16_t)src->now_angular_velocity);
    dst[14] = (uint8_t)(src->vcc_mv >> 8);
    dst[15] = (uint8_t)(src->vcc_mv);
}

/** 16バイトの並びを、構造体に戻します。log_pack の逆です。 */
static void log_unpack(Sensors_data_t *dst, const uint8_t src[LOG_RECORD_SIZE])
{
    dst->sensor_displacement     = src[0];
    dst->sensor_binary           = src[1];
    dst->target_speed            = (uint16_t)(((uint16_t)src[2] << 8) | src[3]);
    dst->left_speed              = (uint16_t)(((uint16_t)src[4] << 8) | src[5]);
    dst->right_speed             = (uint16_t)(((uint16_t)src[6] << 8) | src[7]);
    dst->left_motor_duty         = (int8_t)src[8];
    dst->right_motor_duty        = (int8_t)src[9];
    /* 符号なしの16ビットに組み立ててから、符号付きに読み替えます。 */
    dst->target_angular_velocity = (int16_t)(((uint16_t)src[10] << 8) | src[11]);
    dst->now_angular_velocity    = (int16_t)(((uint16_t)src[12] << 8) | src[13]);
    dst->vcc_mv                  = (uint16_t)(((uint16_t)src[14] << 8) | src[15]);
}

/* ===== 初期化 =============================================================== */

/** 記録の状態を、最初に戻します。記録が止まっているときだけ呼びます。 */
static void reset_state(void)
{
    buf_state = NOT_READY;
    log_full = false;
    dropped_pages = 0;
    capacity_bytes = 0;
    total_bytes = 0;
    flash_state = FLASH_READY;
    busy_is_page_write = false;
    write_addr = 0;
    erased_until = 0;
    pages_written = 0;
    write_errors = 0;
    max_send_us = 0;
    max_busy_us = 0;
    pad_bytes = 0;
}

AT25SF_result_t log_init(void)
{
    logging = false;
    reset_state();
    return AT25SF_init();
}

/* ===== 消去 =================================================================
 * 「どこまで消したか」（erased_until）を覚えておき、その中にしか書きません。
 * 消していない所に書くと、前のデータと混ざった値（ビットごとのAND）になってしまうためです。
 */
AT25SF_result_t log_erase(uint32_t bytes)
{
    if (logging) {
        return AT25SF_ERR_BUSY; /* 記録中は消さない */
    }
    if (bytes == 0) {
        return AT25SF_ERR_PARAM;
    }
    if (bytes > AT25SF_TOTAL_SIZE) {
        bytes = AT25SF_TOTAL_SIZE;
    }

    /* 消す範囲を決めます。
     * (1) 終わりの目印として、1ページ分を多く取ります。log_dump は「全部 FF の件」で
     *     終わりを見つけるので、記録が上限いっぱいまで進んでも、その後ろに必ず
     *     書いていない所が残るようにするためです。これが無いと、上限まで記録したときに、
     *     その先に残っている前回のデータまで、続けて出してしまいます。
     * (2) 4KB（セクタ）の倍数に切り上げます。消去の最小の単位が4KBだからです。
     * (3) メモリの終わりを越えないようにします。
     */
    uint32_t end = bytes + AT25SF_PAGE_SIZE;
    end = (end + AT25SF_SECTOR_SIZE - 1) & ~(AT25SF_SECTOR_SIZE - 1);
    if (end > AT25SF_TOTAL_SIZE) {
        end = AT25SF_TOTAL_SIZE;
    }

    reset_state();

    /* 64KBのブロックで消せる所はブロックで、端数は4KBのセクタで消します。
     * 64KBを4KBずつ消すと 16回×55ms ≒ 0.9秒、ブロックなら1回で約0.2秒です。
     */
    uint32_t addr = 0;
    while (addr < end) {
        AT25SF_result_t r;
        if ((addr % AT25SF_BLOCK_SIZE) == 0 && (end - addr) >= AT25SF_BLOCK_SIZE) {
            r = AT25SF_erase_block(addr);
            addr += AT25SF_BLOCK_SIZE;
        } else {
            r = AT25SF_erase_sector(addr);
            addr += AT25SF_SECTOR_SIZE;
        }
        if (r != AT25SF_OK) {
            return r; /* erased_until は 0 のまま。log_start は false を返す */
        }
    }
    erased_until = end;
    /* 記録に使うのは、目印の1ページを除いた所までです。
     * メモリの終わりまで消したときは、メモリの終わりそのものが目印になります。
     */
    capacity_bytes = (end == AT25SF_TOTAL_SIZE) ? end : end - AT25SF_PAGE_SIZE;
    return AT25SF_OK;
}

/* ===== 記録の開始 =========================================================== */

bool log_start(void)
{
    if (logging) {
        return false;
    }
    /* 消去の直後（まだ何も書いていない状態）からだけ、始められます。
     * 一度止めた後に続きから書くと、途中に「書いていない所」が挟まり、
     * log_dump がそこで終わりだと判断してしまうためです。
     * 電源を入れ直した直後も、前の記録を上書きしないように、消去してからでないと始められません。
     */
    if (erased_until == 0 || total_bytes != 0 || write_addr != 0) {
        return false;
    }
    logging = true;
    return true;
}

/* ===== 記録（割り込みから呼ぶ） ============================================
 * ここでは SPI を使いません。RAMに16バイト書くだけなので、数µsで終わります。
 */
void log_put(const volatile Sensors_data_t *src)
{
    if (!logging) {
        return;
    }

    /* volatile の変数は、1回だけ読んで手元の変数に入れ、最後に1回だけ書き戻します。 */
    uint32_t total = total_bytes;

    if (total >= capacity_bytes) {
        /* 消した範囲を使い切りました。これ以上は受け付けません。 */
        log_full = true;
        logging = false;
        return;
    }

    /* 記録する値の写しを、割り込みを止めている間に取ります。
     * 16ビットの値は、上位のバイトと下位のバイトを別々に取り出して詰めます。
     * その間に、もっと優先度の高い割り込みが値を書き換えると、
     * 古い値の上位と新しい値の下位が混ざってしまうためです（例：0x00FF → 0x0100 の途中で 0x0000）。
     * 止めている時間は、16バイトを写す間だけです。
     */
    Sensors_data_t snap;
    uint32_t ints = save_and_disable_interrupts();
    snap = *src;
    restore_interrupts(ints);

    uint32_t page = (total / AT25SF_PAGE_SIZE) % 2; /* 0 = 箱A、1 = 箱B */
    uint32_t pos = total % AT25SF_PAGE_SIZE;        /* 箱の中の位置      */

    /* &page_buf[page][pos] は「その箱の、その位置」のアドレスです。
     * 渡した先（log_pack）では、そこが dst[0] になるので、dst[0]〜dst[15] に詰めます。
     */
    log_pack(&page_buf[page][pos], &snap);
    total += LOG_RECORD_SIZE;

    if (total % AT25SF_PAGE_SIZE == 0) {
        /* 箱がいっぱいになりました。 */
        if (buf_state != NOT_READY) {
            /* もう一方の箱が、まだ運ばれていません（mainの書き込みが間に合っていない）。
             * 上書きすると運ぶ途中の箱が壊れるので、今いっぱいになった箱の方を捨てます。
             * total を256戻すと、次の呼び出しで、同じ箱の先頭から詰め直しになります。
             * 戻さないと、計算上もう一方の箱（配達待ち）に切り替わってしまいます。
             */
            dropped_pages = dropped_pages + 1;
            total -= AT25SF_PAGE_SIZE;
        } else {
            /* 詰め終えた箱に、札を付けます。次の呼び出しでは、total から
             * 計算される箱の番号が自動で入れ替わるので、切り替えの処理は要りません。
             */
            buf_state = (buf_state_t)page;
        }
    }
    total_bytes = total;
}

/* ===== 書き込み（mainから呼ぶ） =============================================
 * 待たずに少しずつ進めます。mainのループを止めないためです。
 *   FLASH_BUSY  : 前に送ったページを、ICが中で書いている。BUSYが外れたかを1回だけ見る。
 *                 外れていたら FLASH_READY になり、そのまま下の処理へ進む
 *   FLASH_READY : 札の付いた箱があれば送る（送るだけで、書き終わるのは待たない）→ FLASH_BUSY へ
 *
 * 「BUSYが外れたのを見る」と「次の箱を送る」を、1回の呼び出しで続けて行います。
 * 別々の呼び出しに分けると、1ページにつき flash_task を2回呼ぶ必要があり、
 * mainのループが遅いとき（1周が7ms以上）に、書き込みが間に合わなくなるためです。
 */
void flash_task(void)
{
    if (flash_state == FLASH_BUSY) {
        uint32_t elapsed = (uint32_t)(time_us_64() - busy_start_us);
        if (AT25SF_is_busy()) {
            if (elapsed > LOG_PAGE_TIMEOUT_US) {
                /* 長すぎます。ステータスが正しく読めていない疑いがあります。
                 * 止まったままにならないように、数えたうえで READY に戻します。
                 */
                write_errors++;
                flash_state = FLASH_READY;
            }
            return; /* まだ書いている。次の呼び出しで、また見る */
        }
        if (busy_is_page_write && elapsed > max_busy_us) {
            max_busy_us = elapsed;
        }
        flash_state = FLASH_READY;
    }

    /* ここから下は FLASH_READY のとき */

    /* volatile の変数は、1回だけ読んで手元の変数に入れます。
     * 判定した後で値が変わると、判定と違う箱を運んでしまうためです。
     */
    buf_state_t page = buf_state;
    if (page == NOT_READY) {
        return; /* 配達待ちの箱は無い */
    }

    if (write_addr + AT25SF_PAGE_SIZE > capacity_bytes) {
        /* 上限の外には書きません（log_put が先に止めるので、ふつうはここに来ません）。 */
        log_full = true;
        logging = false;
        buf_state = NOT_READY;
        return;
    }

    /* 札に書いてある箱を運びます。
     * 「割り込みが今詰めていない方の箱」と考えて選ぶと、札を付けた直後は
     * 割り込みがまだ同じ箱の前にいるので、反対の箱を運んでしまいます。
     * 運ぶ箱は、札（buf_state）だけを見て決めます。
     */
    uint64_t t0 = time_us_64();
    AT25SF_result_t r = AT25SF_page_program_start(write_addr, page_buf[page], AT25SF_PAGE_SIZE);
    uint64_t t1 = time_us_64();

    if (r == AT25SF_ERR_BUSY) {
        /* ICがまだ忙しいと答えました。札は付けたままにして、BUSYが外れるのを待ってから
         * やり直します。外れないまま時間が過ぎたら、上の FLASH_BUSY の側でエラーに数えます。
         */
        flash_state = FLASH_BUSY;
        busy_is_page_write = false;
        busy_start_us = t1;
        return;
    }
    if (r != AT25SF_OK) {
        write_errors++; /* 受け付けられなかった。このページはあきらめる */
        buf_state = NOT_READY;
        return;
    }

    uint32_t send_us = (uint32_t)(t1 - t0);
    if (send_us > max_send_us) {
        max_send_us = send_us;
    }
    write_addr += AT25SF_PAGE_SIZE; /* アドレスを進めるのは、ここ1か所だけ */
    pages_written++;
    flash_state = FLASH_BUSY;
    busy_is_page_write = true;
    busy_start_us = t1;

    /* 札を外します。データはCSを上げた時点でICの中に送り終えているので、
     * 書き終わるのを待たずに、箱を空けて構いません。
     */
    buf_state = NOT_READY;
}

/** 配達待ちの箱と、ICの中の書き込みが、全部無くなるまで flash_task を回します。 */
static bool drain(uint32_t timeout_us)
{
    uint64_t start = time_us_64();
    while (buf_state != NOT_READY || flash_state != FLASH_READY) {
        flash_task();
        if (time_us_64() - start > timeout_us) {
            return false;
        }
    }
    return true;
}

/* ===== 記録の停止（mainから呼ぶ） =========================================== */

bool log_stop(void)
{
    /* これ以降、割り込みの log_put は何もしません。
     * 割り込みはmainを止めて動きますが、mainが割り込みの途中に割り込むことはありません。
     * なので、logging を false にした後に読んだ total_bytes は、もう変わりません。
     * （どちらも volatile なので、「止める → 読む」の順番が入れ替わることもありません）
     */
    logging = false;
    uint32_t total = total_bytes;

    /* まず、配達待ちの箱を運び終えます。 */
    if (!drain(LOG_DRAIN_TIMEOUT_US)) {
        return false;
    }

    /* 詰めている途中の箱が残っていたら、残りを 0xFF で埋めて運びます。
     * 0xFF は「書いていない」のと同じ値なので、log_dump はそこで終わりだと判断します。
     * （フラッシュは、0xFF を書いても中身が変わりません）
     */
    uint32_t pos = total % AT25SF_PAGE_SIZE;
    if (pos != 0) {
        uint32_t page = (total / AT25SF_PAGE_SIZE) % 2;
        memset(&page_buf[page][pos], 0xFF, AT25SF_PAGE_SIZE - pos);
        pad_bytes = AT25SF_PAGE_SIZE - pos;
        total_bytes = total + pad_bytes; /* ページの終わりまで進めておく */
        buf_state = (buf_state_t)page;   /* 割り込みはもう札を触らないので、mainが付ける */
        if (!drain(LOG_DRAIN_TIMEOUT_US)) {
            return false;
        }
    }
    return write_errors == 0;
}

/* ===== 読み出し（mainから呼ぶ） ============================================= */

static bool is_all_ff(const uint8_t *p, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (p[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

void log_dump(void)
{
    if (logging) {
        printf("記録中は読み出せません。先に log_stop() を呼んでください。\n");
        return;
    }
    /* 読む前に、ICと通信できているかを確かめます。
     * 配線が外れていると、読んだ値が全部 00 などになり、「書いていない所（全部 FF）」が
     * いつまでも見つからず、意味のない行をメモリの終わりまで出し続けてしまうためです。
     */
    if (AT25SF_check_id() != AT25SF_OK) {
        printf("ICと通信できていません（JEDEC ID が違います）。配線を確かめてください。\n");
        return;
    }
    /* 書き込みが残っていたら、終わるのを待ちます。 */
    if (!drain(LOG_DRAIN_TIMEOUT_US)) {
        printf("書き込みが終わりません（ICがBUSYのままです）。\n");
        return;
    }

    /* 256バイトの配列は、関数の中に置くとスタックを使います。
     * static にして、静的な領域に置いています。
     */
    static uint8_t buf[AT25SF_PAGE_SIZE];
    uint32_t count = 0;
    bool end_found = false;

    /* PicoのプログラムからPCのファイルを開くことはできないので、文字（CSV）で出し、
     * PC側（シリアルモニタのログ機能など）で保存します。
     * 生のバイトのまま出すと、端末で読めないうえ、値が 0x0A のバイトが改行として
     * 扱われ、その前に 0x0D が足されてしまいます（Pico SDK の標準出力の、既定の動き）。
     * 目印の行と見出しは、PC側で切り出しやすいように、英数字だけにしています。
     */
    printf("=== START DUMP ===\n");
    printf("sensor_displacement,sensor_binary,target_speed,left_speed,right_speed,"
           "left_motor_duty,right_motor_duty,target_angular_velocity,now_angular_velocity,vcc_mv\n");

    for (uint32_t addr = 0; addr < AT25SF_TOTAL_SIZE && !end_found; addr += AT25SF_PAGE_SIZE) {
        AT25SF_result_t r = AT25SF_read(addr, buf, AT25SF_PAGE_SIZE);
        if (r != AT25SF_OK) {
            printf("読み出しエラー : %s\n", AT25SF_result_name(r));
            break;
        }
        for (uint32_t i = 0; i < AT25SF_PAGE_SIZE; i += LOG_RECORD_SIZE) {
            if (is_all_ff(&buf[i], LOG_RECORD_SIZE)) {
                end_found = true; /* 書いていない所まで来た */
                break;
            }
            Sensors_data_t rec;
            log_unpack(&rec, &buf[i]);
            /* printf の %u と %d は、unsigned int と int を受け取ります。
             * 型をはっきりさせるために、キャストして渡しています。
             */
            printf("%u,%u,%u,%u,%u,%d,%d,%d,%d,%u\n",
                   (unsigned)rec.sensor_displacement, (unsigned)rec.sensor_binary,
                   (unsigned)rec.target_speed, (unsigned)rec.left_speed, (unsigned)rec.right_speed,
                   (int)rec.left_motor_duty, (int)rec.right_motor_duty,
                   (int)rec.target_angular_velocity, (int)rec.now_angular_velocity,
                   (unsigned)rec.vcc_mv);
            count++;
        }
    }
    printf("=== END DUMP ===\n");
    printf("records=%lu\n", (unsigned long)count);
}

bool log_read_record(uint32_t index, Sensors_data_t *out)
{
    uint8_t raw[LOG_RECORD_SIZE];

    if (logging || out == NULL || index >= AT25SF_TOTAL_SIZE / LOG_RECORD_SIZE) {
        return false;
    }
    if (AT25SF_read(index * LOG_RECORD_SIZE, raw, sizeof(raw)) != AT25SF_OK) {
        return false;
    }
    if (is_all_ff(raw, sizeof(raw))) {
        return false; /* 書いていない所 */
    }
    log_unpack(out, raw);
    return true;
}

/* ===== 状態の読み出し ======================================================= */

void log_get_stats(log_stats_t *out)
{
    out->records = (total_bytes - pad_bytes) / LOG_RECORD_SIZE;
    out->pages_written = pages_written;
    out->dropped_pages = dropped_pages;
    out->write_errors = write_errors;
    out->max_send_us = max_send_us;
    out->max_busy_us = max_busy_us;
    out->write_addr = write_addr;
    out->capacity_bytes = capacity_bytes;
    out->erased_until = erased_until;
    out->logging = logging;
    out->full = log_full;
}
