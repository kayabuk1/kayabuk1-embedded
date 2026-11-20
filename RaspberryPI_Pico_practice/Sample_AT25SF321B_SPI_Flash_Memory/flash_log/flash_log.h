/**
 * @file  flash_log.h
 * @brief 1msごとの記録を、フラッシュメモリにためるログ 【ログの層】
 *
 * この層の役割は、「16バイトの記録をためて、256バイト（1ページ）ずつフラッシュに書く」
 * ことと、「読み出してCSVで出す」ことです。
 * スイッチ、ブザー、LED、タイマーのことは知りません。いつ記録を始めて、いつ止めるかは、
 * 使う側（main.c や走行のプログラム）が決めます。
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
 *     log_init();              // 起動時に1回。ICと通信できるかも確かめる
 *     log_erase(バイト数);     // 記録する範囲を消す（走る前に済ませる）
 *     log_start();             // 記録を始める
 *       … 1msの割り込みから log_put(&now_sensors_data);
 *       … main のループから flash_task();
 *     log_stop();              // 記録を止め、残りを書き終える
 *     log_dump();              // CSVで出す（電源を入れ直した後でも読めます）
 *
 *   1回の log_erase() につき、記録は1回です。続きから書き足すことはできません。
 *
 * ■ 守ること
 *   1. log_put() を呼ぶのは1か所だけ（1msの割り込み）にする。
 *   2. flash_task()、log_erase()、log_stop()、log_dump() は main から呼ぶ（割り込みの中から呼ばない）。
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
    uint8_t  sensor_displacement;      /* センサーのずれ                 */
    uint8_t  sensor_binary;            /* センサーの2値（ビットごと）    */
    uint16_t target_speed;             /* 目標の速さ                     */
    uint16_t left_speed;               /* 左の速さ                       */
    uint16_t right_speed;              /* 右の速さ                       */
    int8_t   left_motor_duty;          /* 左モータのduty（逆回転は負）   */
    int8_t   right_motor_duty;         /* 右モータのduty（逆回転は負）   */
    int16_t  target_angular_velocity;  /* 目標の角速度                   */
    int16_t  now_angular_velocity;     /* 今の角速度                     */
    uint16_t vcc_mv;                   /* 電源電圧 [mV]                  */
} Sensors_data_t;

#define LOG_RECORD_SIZE   16u
#define LOG_BYTES_PER_SEC (1000u * LOG_RECORD_SIZE) /* 1msごとに記録すると、1秒で16000バイト */

_Static_assert(sizeof(Sensors_data_t) == LOG_RECORD_SIZE, "Sensors_data_t must be 16 bytes");

/* みんなで値を入れる、今の記録です。
 * それぞれの担当が、自分の項目（例：now_sensors_data.left_speed）に値を書き込み、
 * 1msの割り込みの最後に log_put(&now_sensors_data); を呼ぶ、という使い方を想定しています。
 * 割り込みとmainの両方から触るので volatile を付けています。
 * 別の変数を用意して log_put() に渡しても構いません。
 */
extern volatile Sensors_data_t now_sensors_data;

/* ===== 状態の読み出し ======================================================= */
typedef struct {
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
    uint32_t capacity_bytes; /* 記録できる上限のバイト数                               */
    uint32_t erased_until;   /* 消去が済んでいる範囲の終わり                           */
    bool     logging;        /* 記録中なら true                                        */
    bool     full;           /* 上限まで使い切って、止まったら true                    */
} log_stats_t;

/* ===== 関数 ================================================================= */

/** 起動時に1回呼びます。ドライバを初期化し、通信できたかを返します。 */
AT25SF_result_t log_init(void);

/**
 * 先頭から、bytes バイト以上を記録できるように消去し、記録の準備をします。mainから呼びます。
 * 例：60秒分なら log_erase(60 * LOG_BYTES_PER_SEC);
 * 消去には時間がかかるので（64KBにつき標準200ms、最大700ms）、走る前に済ませてください。
 * 走りながら消すと、その間は書けず、バッファがあふれます。
 * 実際に消す範囲は、4KBの倍数に切り上げ、さらに終わりの目印として1ページ分多くなります。
 */
AT25SF_result_t log_erase(uint32_t bytes);

/** 記録を始めます。log_erase() の後でなければ false を返します。 */
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
 * 全部書けたら true。
 */
bool log_stop(void);

/**
 * 記録を、先頭から順にCSVで出します。mainから呼びます。
 * 16バイト全部が 0xFF の件（＝書いていない所）が出たら終わりにするので、
 * 電源を入れ直した後でも、どこまで書いたかが分かります。
 */
void log_dump(void);

/**
 * index 番目（0から）の記録を1件読みます。mainから呼びます。
 * 書かれていない所、範囲の外、記録中のときは false を返します。
 */
bool log_read_record(uint32_t index, Sensors_data_t *out);

/** 今の状態を out に入れます。 */
void log_get_stats(log_stats_t *out);

#endif /* FLASH_LOG_H */
