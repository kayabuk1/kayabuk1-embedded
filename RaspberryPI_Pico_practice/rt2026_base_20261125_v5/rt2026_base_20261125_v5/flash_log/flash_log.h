/**
 * @file  flash_log.h
 * @brief 1msごとの記録を、フラッシュメモリにためるログ 【ログの層】
 *
 * この層の役割は、「16バイトの記録をためて、256バイト（1ページ）ずつフラッシュに書く」
 * ことと、「読み出してCSVで出す」ことです。
 * スイッチ、ブザー、LED、タイマーのことは知りません。いつ記録を始めて、いつ止めるかは、
 * 使う側（main.c や走行のプログラム）が決めます。
 *
 * ■ 1回の全消去で、走行を5回まで記録できます（2026-11-24 に追加）
 *   フラッシュメモリ（4MB）を、走行5回分の場所と、管理用の場所に分けてあります。
 *   分け方は、下の「メモリの割り当て」の表のとおりです。
 *   log_start() を呼ぶたびに、走行1、走行2 … の順に、次の場所へ記録します。
 *   5回分を使い切ったら、log_erase_all() で全体を消すまで、記録は始められません。
 *   どの場所まで使ったかは、電源を切っても、フラッシュの中身から分かります。
 *
 * ■ なぜ、ためてから書くのか
 *   フラッシュへの書き込みはSPIを使い、1ページで数msかかります。
 *   これを1msの制御割り込みの中で行うと、制御が間に合わなくなります。
 *   そこで役割を2つに分けています。
 *
 *     割り込み（1msごと） : log_put()    … 16バイトをRAMのバッファに詰めるだけ。数µsで終わる
 *     main のループ       : flash_task() … たまった1ページを、フラッシュに書く
 *
 *   バッファは2枚（AとB）あります。割り込みがAに詰めている間に、mainはBを書きます。
 *   1枚がいっぱいになるのは16msごとなので、mainは16ms以内に1ページ書けば間に合います。
 *
 * ■ 使い方（順番が大事です）
 *     log_init();              // 起動時に1回。ICと通信できるか、どの走行まで使ったかを調べる
 *     log_erase_all();         // 全体を消す（標準10秒、最大30秒）。走る前に済ませる
 *     log_start();             // 次の走行の場所に、記録を始める
 *       … 1msの割り込みから log_put(&now_sensors_data);
 *       … main のループから flash_task();
 *     log_stop();              // 記録を止め、残りを書き終える
 *     log_dump(1);             // 走行1をCSVで出す（電源を入れ直した後でも読めます）
 *
 * ■ 守ること
 *   1. log_put() を呼ぶのは1か所だけ（1msの割り込み）にする。
 *   2. log_put() 以外の関数は main から呼ぶ（割り込みの中から呼ばない）。
 *   3. main のループは、flash_task() を10ms以内の間隔で呼ぶ。
 *      1ページは16msでいっぱいになるので、それまでに前のページを送り終える必要があります。
 *      長い printf や sleep をループに入れると、書き込みが間に合わず、ページを捨てます
 *      （捨てた数は log_get_stats() の dropped_pages で分かります。0が正常です）。
 *   4. フラッシュのSPIポート（AT25SF_SPI_PORT）を、割り込みの中で使うデバイス
 *      （A/Dコンバータやジャイロなど）と共有しない。共有すると、mainがフラッシュに
 *      送っている途中に割り込みが同じポートを使い、両方の通信が壊れます。
 *   5. log_put() を呼ぶ割り込みと flash_task() は、同じコアで動かす（マルチコアにしない）。
 */
#ifndef FLASH_LOG_H
#define FLASH_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "at25sf.h"

/* ===== 1件の記録 ============================================================
 * 1件を16バイトにしてあります。256バイトの1ページに、ちょうど16件入ります。
 * 件がページの境界をまたがないので、アドレスの計算が単純になります。
 * 項目を変えるときは、合計が16バイトのままになるようにして、flash_log.c の
 * log_pack / log_unpack / log_dump の3か所も合わせて直してください
 * （大きさは、下の _Static_assert がビルドのときに確かめます）。
 * 値の単位（速さを mm/s にするか、エンコーダの数にするか、など）は、使う側で決めます。
 */
typedef struct {
    int8_t   sensor_displacement;      /* ラインからのずれ（-100〜+100）  */
    uint8_t  sensor_binary;            /* センサーの2値（ビットごと）    */
    int16_t  target_speed;             /* 目標の速さ [mm/s]              */
    int16_t  left_speed;               /* 左の速さ [mm/s]（後退は負）    */
    int16_t  right_speed;              /* 右の速さ [mm/s]（後退は負）    */
    int8_t   left_motor_duty;          /* 左モータのduty（逆回転は負）   */
    int8_t   right_motor_duty;         /* 右モータのduty（逆回転は負）   */
    int16_t  target_angular_velocity;  /* 目標の角速度 [deg/s]（左回りが正） */
    int16_t  now_angular_velocity;     /* 今の角速度 [deg/s]。ジャイロを使わないときは、車輪の速さの差から計算した値 */
    uint16_t vcc_mv;                   /* 電源電圧 [mV]                  */
} Sensors_data_t;

#define LOG_RECORD_SIZE   16u
#define LOG_BYTES_PER_SEC (1000u * LOG_RECORD_SIZE) /* 1msごとに記録すると、1秒で16000バイト */

_Static_assert(sizeof(Sensors_data_t) == LOG_RECORD_SIZE, "Sensors_data_t must be 16 bytes");

/* ===== メモリの割り当て（先生の記録例の表のとおり） ==========================
 *
 *   場所     アドレス                 大きさ     1msごとに記録できる時間
 *   走行1    0x000000 〜 0x0FBFFF     1008KiB    64.512秒（64512件）
 *   走行2    0x0FC000 〜 0x1BAFFF      764KiB    48.896秒（48896件）
 *   走行3    0x1BB000 〜 0x279FFF      764KiB    48.896秒
 *   走行4    0x27A000 〜 0x338FFF      764KiB    48.896秒
 *   走行5    0x339000 〜 0x3F7FFF      764KiB    48.896秒
 *   管理     0x3F8000 〜 0x3FFFFF       32KiB    先頭の16バイトが「全消去が済んだ」目印。
 *                                                残りは、コース情報などのために空けてあります
 *
 * 走行の場所は、先頭から詰めて書きます。場所の終わりまで書いたら、その走行の記録は止まります
 * （次の走行の場所には、はみ出しません）。
 */
#define LOG_RUN_NUM    5u               /* 記録できる走行の回数                */
#define LOG_RUN1_BYTES (1008u * 1024u)  /* 走行1の場所の大きさ [バイト]        */
#define LOG_RUNN_BYTES (764u * 1024u)   /* 走行2〜5の場所の大きさ [バイト]     */
#define LOG_MGMT_ADDR  (LOG_RUN1_BYTES + (LOG_RUN_NUM - 1u) * LOG_RUNN_BYTES) /* 管理の場所の先頭 */

_Static_assert(LOG_MGMT_ADDR == 0x3F8000u, "memory map: management area must start at 0x3F8000");
_Static_assert(LOG_RUN1_BYTES % 4096u == 0u && LOG_RUNN_BYTES % 4096u == 0u, "memory map: sizes must be multiples of 4KB");

/* みんなで値を入れる、今の記録です。
 * それぞれの担当が、自分の項目（例：now_sensors_data.left_speed）に値を書き込み、
 * 1msの割り込みの最後に log_put(&now_sensors_data); を呼ぶ、という使い方を想定しています。
 * 割り込みとmainの両方から触るので volatile を付けています。
 * 別の変数を用意して log_put() に渡しても構いません。
 */
extern volatile Sensors_data_t now_sensors_data;

/* ===== 状態の読み出し ======================================================= */
typedef struct {
    /* --- 全体の状態 --- */
    bool     formatted;      /* 全消去が済んでいて、記録を始められる並びになっていれば true   */
    uint32_t used_runs;      /* 記録が入っている走行の数（0〜5）。次に記録するのは、この次の番号 */
    /* --- 記録中の走行、または、最後に記録した走行について --- */
    uint32_t run;            /* その走行の番号（1〜5）。電源を入れてからまだ記録していなければ 0 */
    uint32_t records;        /* フラッシュに残る件数（捨てたページの分は含まない）     */
    uint32_t pages_written;  /* フラッシュに書いたページ数                             */
    uint32_t dropped_pages;  /* mainの書き込みが間に合わず、捨てたページ数（0が正常）  */
    uint32_t write_errors;   /* 書き込みが受け付けられなかった回数（0が正常）。
                                送り直しても書けずに、あきらめたページの数です          */
    uint32_t write_retries;  /* ICが受け付けなかったページを、送り直した回数（0が正常）。
                                0でなければ、ICへの命令が届かないことがあります。
                                送り直して書けた分は、write_errors には数えません       */
    /* --- 時間。「1ページあたり」の値と、「全ページの合計」の値があります --- */
    uint32_t max_send_us;    /* 1ページをICへ送るのにかかった時間の、全ページの中の最大 [µs]。
                                06h → ステータスの確認 → 02h+アドレス+256バイト を送り終えるまで */
    uint32_t total_send_us;  /* 上の「1ページをICへ送る時間」を、全ページ分足した合計 [µs]。
                                この時間、mainはSPIの送信にかかりきりになります                */
    uint32_t max_busy_us;    /* 1ページを送り終えてから、flash_task が BUSY=0 を確認するまでの
                                時間の、全ページの中の最大 [µs]（ICが中で書いている時間）      */
    uint32_t log_time_ms;    /* log_start() から、log_stop() が最後のページを書き終えるまでの
                                時間 [ms]。まだ log_stop() を呼んでいなければ 0                */
    uint32_t write_addr;     /* 次に書くアドレス                                       */
    uint32_t capacity_bytes; /* その走行の場所の大きさ [バイト]                        */
    bool     logging;        /* 記録中なら true                                        */
    bool     full;           /* 場所の終わりまで使い切って、止まったら true            */
} log_stats_t;

/* ===== 関数 ================================================================= */

/**
 * 起動時に1回呼びます。ドライバを初期化し、通信できたかを返します。
 * 通信できたら、フラッシュの中身を調べて、どの走行まで記録が入っているかを数えます。
 */
AT25SF_result_t log_init(void);

/**
 * フラッシュメモリの全体を消去します。mainから呼びます。標準10秒、最大30秒かかります。
 * 終わると、走行1から5回分を記録できます。前の記録は、5回分とも消えます。
 * 記録中に呼ぶと、何もせずに AT25SF_ERR_BUSY を返します。
 */
AT25SF_result_t log_erase_all(void);

/**
 * 次に記録する走行の番号（1〜5）を返します。
 * 記録を始められないとき（全消去がまだ、または、5回分を使い切った）は 0 を返します。
 */
int log_next_run(void);

/**
 * 記録を始めます。場所は、log_next_run() が返す走行の場所です。
 * 始められないとき（log_next_run() が 0 のとき、記録中のとき）は false を返します。
 */
bool log_start(void);

/**
 * 1件を記録します。1msの割り込みの中から呼びます。
 * RAMのバッファに詰めるだけで、SPIは使いません。記録中でなければ、何もしません。
 */
void log_put(const volatile Sensors_data_t *src);

/**
 * mainのループの中で、毎回呼びます。すぐに戻ります。
 * 書き込みを待っているページがあれば、フラッシュに送ります。
 */
void flash_task(void);

/**
 * 記録を止め、バッファに残っている分をフラッシュに書き終えます。mainから呼びます。
 * 全部書けたら true。1件も書いていなければ、走行の場所は使われません（次の log_start() も同じ場所になります）。
 */
bool log_stop(void);

/**
 * 走行 run（1〜5）の場所に入っている件数を、フラッシュの中身から数えます。mainから呼びます。
 * 電源を入れ直した後でも数えられます。記録中と、範囲の外の番号には 0 を返します。
 */
uint32_t log_run_records(int run);

/** 走行 run（1〜5）の場所に入る件数の上限を返します（走行1は 64512、走行2〜5は 48896）。 */
uint32_t log_run_capacity(int run);

/**
 * 走行 run（1〜5）の記録を、先頭から順にCSVで出します。mainから呼びます。
 * 16バイト全部が 0xFF の件（＝書いていない所）が出たら終わりにするので、
 * 電源を入れ直した後でも、どこまで書いたかが分かります。
 */
void log_dump(int run);

/**
 * 走行 run（1〜5）の、index 番目（0から）の記録を1件読みます。mainから呼びます。
 * 書かれていない所、範囲の外、記録中のときは false を返します。
 */
bool log_read_record(int run, uint32_t index, Sensors_data_t *out);

/** 今の状態を out に入れます。 */
void log_get_stats(log_stats_t *out);

#endif /* FLASH_LOG_H */
