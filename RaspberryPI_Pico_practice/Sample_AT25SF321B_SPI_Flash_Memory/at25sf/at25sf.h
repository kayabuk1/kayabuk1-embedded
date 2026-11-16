/**
 * @file  at25sf.h
 * @brief AT25SF321B（SPIフラッシュメモリ 32Mbit = 4MB）のドライバ 【ドライバの層】
 *
 * この層の役割は「SPIでICと話す」ことだけです。
 * ログの形（16バイトの記録）、バッファ、スイッチ、ブザーのことは知りません。
 * そうしておくと、ログ以外の用途（設定の保存など）にも、そのまま使えます。
 *
 * ■ 使うときの約束
 *   1. この層の関数は、main側（割り込みの外）から呼ぶ。
 *      割り込みの中から呼ぶと、mainの通信の途中でCSが切り替わり、通信が壊れる。
 *   2. 書く前に、その場所を消去しておく。
 *      フラッシュは、ビットを 1→0 にしか書けない。0→1 に戻せるのは消去だけ。
 *   3. Page Program は、1回に256バイトまで。ページの境界（256の倍数）をまたげない。
 *      （またぐと、同じページの先頭に戻って上書きされる。データシート 8.1）
 *
 * ■ 配線
 *   ICのピン            ブレッドボード (PICO2350 = 0)   本番の機体 (PICO2350 = 1)
 *   1 CS#               GPIO3                           GPIO13
 *   2 SO  (IC → Pico)   GPIO4  (SPI0 RX)                GPIO12 (SPI1 RX)
 *   3 WP#               つながなくてよい（下の注1）
 *   4 GND               GND
 *   5 SI  (Pico → IC)   GPIO7  (SPI0 TX)                GPIO11 (SPI1 TX)
 *   6 SCK               GPIO6  (SPI0 SCK)               GPIO10 (SPI1 SCK)
 *   7 HOLD#             つながなくてよい（下の注1）
 *   8 VCC               3.3V
 *
 *   注1: WP# と HOLD# は、ICの中でプルアップされていて、使わないなら浮かせたままで
 *        よいとされています（データシート Table 1）。ブレッドボードで動きが安定しない
 *        ときは、3.3V につなぐと、ノイズを拾いにくくなります（一般的な対策）。
 *   注2: データシートは、CS# を 10kΩ で VCC につなぐこと（プルアップ）を勧めています。
 *        電源を入れた直後、PicoがCSを動かす前に、CSが浮いて誤動作するのを防ぐためです。
 *        ブレッドボードの試験では無くても動きますが、基板にするときは付けてください。
 */
#ifndef AT25SF_H
#define AT25SF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ===== ピンとSPIの設定 =====================================================
 * 基板に合わせて、ここだけを変えます。
 * PICO2350 を 1 にすると、本番の機体のピンになります。
 * #ifndef で囲んであるので、CMake 側から -DPICO2350=1 のように指定もできます。
 */
#ifndef PICO2350
#define PICO2350 0
#endif

#if PICO2350
#define AT25SF_SPI_PORT spi1
#define AT25SF_PIN_SCK  10
#define AT25SF_PIN_SI   11
#define AT25SF_PIN_SO   12
#define AT25SF_PIN_CS   13
#else
#define AT25SF_SPI_PORT spi0
#define AT25SF_PIN_SCK  6
#define AT25SF_PIN_SO   4
#define AT25SF_PIN_SI   7
#define AT25SF_PIN_CS   3
#endif

/* SPIの速さ。
 * まず確実に動く 1MHz にしてあります（実機で JEDEC ID が読めた実績のある速さ）。
 * 1MHz でも、1ページ（260バイト）の送信は約2.1msで、16msごとのページ書き込みには間に合います。
 * 自己診断（main.c の 't' コマンド）が通ることを確かめてから、10MHz などに上げ、
 * 上げた後にもう一度 't' を通してください。
 * ブレッドボードや長いジャンパ線では、速くするほど波形が崩れやすくなります。
 * IC側の上限は、読み出し（03h）が55MHzです（データシート 13.3）。
 */
#ifndef AT25SF_SPI_BAUDRATE
#define AT25SF_SPI_BAUDRATE (1000 * 1000)
#endif

/* ===== メモリの大きさ =======================================================
 * 数字に名前を付けておくと、256 や 4096 が何の数字かを、読む人が迷いません。
 * どれも2のべき乗なので、割り算と余りは、シフトとマスクで計算できます。
 *   ページ番号 = addr >> 8    ページ内の位置 = addr & 0xFF
 *   セクタ番号 = addr >> 12   ブロック番号   = addr >> 16
 */
#define AT25SF_PAGE_SIZE   256u       /* Page Program の単位             */
#define AT25SF_SECTOR_SIZE 4096u      /* いちばん小さい消去の単位（4KB） */
#define AT25SF_BLOCK_SIZE  65536u     /* 大きい消去の単位（64KB）        */
#define AT25SF_TOTAL_SIZE  0x400000u  /* 全体（4MB）。アドレスは 0x000000〜0x3FFFFF */

/* ===== JEDEC ID =============================================================
 * 9Fh で読める3バイトです。メーカー（1F）と、ICの種類（87 01）を表します。
 * 通信できているかの確認に使います。
 */
#define AT25SF_ID_BYTE0 0x1Fu
#define AT25SF_ID_BYTE1 0x87u
#define AT25SF_ID_BYTE2 0x01u

/* ===== ステータスレジスタ1のビット ========================================= */
#define AT25SF_SR1_BUSY    0x01u  /* bit0 RDY/BSY : 1 = 書き込み・消去の最中         */
#define AT25SF_SR1_WEL     0x02u  /* bit1 WEL     : 1 = 書き込み許可（06h で 1 になる） */
#define AT25SF_SR1_BP_MASK 0x7Cu  /* bit2〜6 BP0〜BP4 : 1 があると、保護された範囲は書けない */
#define AT25SF_SR1_SRP0    0x80u  /* bit7 SRP0    : ステータスレジスタ自体の保護     */

/* ===== 結果 =================================================================
 * 成功か失敗かだけでなく、「どこで失敗したか」を返します。
 * 実機で動かないときに、配線なのか、許可なのか、保護なのかを切り分けるためです。
 */
typedef enum {
    AT25SF_OK = 0,
    AT25SF_ERR_ID,          /* JEDEC ID が 1F 87 01 ではない（配線・速さ・電源を疑う）       */
    AT25SF_ERR_BUSY,        /* 前の書き込み・消去がまだ終わっていない                        */
    AT25SF_ERR_NO_WEL,      /* 06h を送っても WEL が 1 にならない（命令が届いていない）      */
    AT25SF_ERR_PROTECTED,   /* 保護ビット（BP0〜BP4）が立っている。AT25SF_unprotect() で解除する */
    AT25SF_ERR_NOT_STARTED, /* 命令を送ったのに BUSY にならない（受け付けられていない）       */
    AT25SF_ERR_TIMEOUT,     /* 上限時間まで待っても BUSY が外れない（ステータスが読めていない） */
    AT25SF_ERR_PARAM,       /* 引数が範囲外（アドレス、長さ、ページの境界）                  */
} AT25SF_result_t;

/** 結果を、表示用の文字列にします（printf の %s に渡せます）。 */
const char *AT25SF_result_name(AT25SF_result_t r);

/* ===== 初期化と確認 ========================================================= */

/** SPIとピンを設定し、JEDEC ID を読んで通信を確かめます。IDが違えば AT25SF_ERR_ID。 */
AT25SF_result_t AT25SF_init(void);

/** JEDEC ID を3バイト読みます。AT25SF321B なら 1F 87 01。 */
void AT25SF_read_id(uint8_t id[3]);

/** JEDEC ID を読み、AT25SF321B のものなら AT25SF_OK、違えば AT25SF_ERR_ID を返します。 */
AT25SF_result_t AT25SF_check_id(void);

/** 実際に設定されたSPIの速さ（Hz）。指定した値ちょうどにはならないことがあります。 */
uint32_t AT25SF_get_baudrate(void);

/* ===== ステータス =========================================================== */

uint8_t AT25SF_read_status1(void);
uint8_t AT25SF_read_status2(void);

/** 書き込み・消去の最中なら true。 */
bool AT25SF_is_busy(void);

/** BUSY が外れるまで待ちます。timeout_us を過ぎたら AT25SF_ERR_TIMEOUT。 */
AT25SF_result_t AT25SF_wait_ready(uint32_t timeout_us);

/** 書き込み許可（06h）を送ります。確認用に公開しています（ふだんは下の関数が中で呼びます）。 */
void AT25SF_write_enable(void);

/* ===== 読み出し ============================================================= */

/**
 * addr から len バイトを buf に読みます。ページの境界は気にしなくて構いません。
 * 書き込み・消去の最中は読めないので、AT25SF_ERR_BUSY を返します。
 */
AT25SF_result_t AT25SF_read(uint32_t addr, uint8_t *buf, size_t len);

/* ===== 書き込み ============================================================= */

/**
 * 1ページ以内（最大256バイト）を書きます。終わるまで待ちます（標準0.4ms、最大3.4ms）。
 * addr から len バイトが、同じページに収まっている必要があります。
 */
AT25SF_result_t AT25SF_page_program(uint32_t addr, const uint8_t *data, size_t len);

/**
 * 上と同じですが、送るだけで、終わるのを待ちません。
 * 走行中のログのように、mainを止めたくないときに使います。
 * 終わったかどうかは、AT25SF_is_busy() で確かめます。
 */
AT25SF_result_t AT25SF_page_program_start(uint32_t addr, const uint8_t *data, size_t len);

/* ===== 消去 =================================================================
 * 消去すると、その範囲が全部 0xFF になります。終わるまで待ちます。
 * addr は、消したい範囲の中のどこを指しても構いません（下位のビットは無視されます）。
 */
AT25SF_result_t AT25SF_erase_sector(uint32_t addr); /* 4KB  : 標準55ms、 最大250ms */
AT25SF_result_t AT25SF_erase_block(uint32_t addr);  /* 64KB : 標準200ms、最大700ms */
AT25SF_result_t AT25SF_erase_chip(void);            /* 全体 : 標準10秒、 最大30秒  */

/* ===== 保護の解除 ===========================================================
 * ステータスレジスタ1に 0x00 を書き、保護ビット（BP0〜BP4）を全部 0 にします。
 * 保護ビットは「この範囲には書かせない」という設定で、立っていると、ICはその範囲への
 * 書き込みと消去を黙って無視します（全消去は、1か所でも保護されていると実行されません）。
 * このドライバは、保護ビットが1つでも立っていたら、書き込みと消去を送らずに
 * AT25SF_ERR_PROTECTED を返します。黙って無視されるより、原因が分かりやすいためです。
 * 【注意】ステータスレジスタは、電源を切っても残ります。
 * AT25SF_ERR_PROTECTED が出たとき（自己診断で保護ビットが立っていると出たとき）だけ使ってください。
 */
AT25SF_result_t AT25SF_unprotect(void);

#endif /* AT25SF_H */
