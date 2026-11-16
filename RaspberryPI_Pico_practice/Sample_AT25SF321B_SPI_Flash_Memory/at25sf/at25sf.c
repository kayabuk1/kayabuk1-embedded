/**
 * @file  at25sf.c
 * @brief AT25SF321B のドライバ 【ドライバの層】
 *
 * ■ このファイルの組み立て方
 *   このICへの命令は、線の上を流れる形で見ると、次の6つのどれかです。
 *
 *     ① [op]                          06h(書き込み許可)  C7h(全消去)
 *     ② [op][← 読む …]                05h(ステータス)    9Fh(JEDEC ID)
 *     ③ [op][書く … →]                01h(ステータスの書き込み)
 *     ④ [op][アドレス×3]              20h(4KB消去)  D8h(64KB消去)
 *     ⑤ [op][アドレス×3][書く … →]    02h(ページ書き込み)
 *     ⑥ [op][アドレス×3][← 読む …]    03h(読み出し)
 *
 *   そこで、形ごとに関数を1つずつ作り（send_op〜）、残りの関数は
 *   「どのオペコードを、どの形で送るか」を書くだけにしています。
 *   CSを下げる・上げる、アドレスを3バイトに分ける、という間違えやすい処理が、
 *   それぞれ1か所だけになります。
 *
 * ■ 11/16の版（1つのファイルだった頃）の名前との対応
 *   このファイルの中だけで使う関数は static にして、短い名前にしました。
 *   static の関数は、ほかのファイルから見えないので、名前がぶつかる心配がありません。
 *
 *     AT25SF_cs_low / AT25SF_cs_high   → cs_low / cs_high
 *     AT25SF_only_send_opecode         → send_op              ①
 *     AT25SF_only_read                 → send_op_read         ②
 *     AT25SF_send_opecode_addr         → send_op_addr         ④
 *     AT25SF_addr_write                → send_op_addr_write   ⑤
 *     AT25SF_addr_read                 → send_op_addr_read    ⑥
 *     read_status_register_1           → AT25SF_read_status1
 *     read_status_is_busy              → AT25SF_is_busy
 *     wait_ready                       → AT25SF_wait_ready
 *     AT25SF_erase_all                 → AT25SF_erase_chip
 *     AT25SF_print_id_status           → main.c の print_id_status（表示はアプリの層へ）
 *
 *   このファイルは printf を使いません。表示するかどうかは、使う側が決めます。
 *   頻繁に呼ばれる関数（BUSYの確認など）の中で表示すると、その時間だけ処理が止まり、
 *   時間の計測も狂うためです。
 */
#include "at25sf/at25sf.h"

#include "hardware/spi.h"
#include "pico/stdlib.h"

/* ===== オペコード（このファイルで使うものだけ） ============================
 * 値は、データシートの Table 4（コマンドの一覧）で確かめてあります。
 */
#define OP_WRITE_STATUS1 0x01
#define OP_PAGE_PROGRAM  0x02
#define OP_READ_DATA     0x03
#define OP_READ_STATUS1  0x05
#define OP_WRITE_ENABLE  0x06
#define OP_ERASE_4K      0x20
#define OP_READ_STATUS2  0x35
#define OP_JEDEC_ID      0x9F
#define OP_ERASE_CHIP    0xC7
#define OP_ERASE_64K     0xD8

/* ===== 待ち時間の上限 =======================================================
 * データシート 13.4 の「最大値」に、余裕を足した値です。
 * 上限を付けるのは、配線が外れるなどしてステータスが読めないときに、
 * 永久に止まらないようにするためです。
 */
#define TIMEOUT_PAGE_US      (10u * 1000u)          /* ページ書き込み  最大3.4ms */
#define TIMEOUT_WRSR_US      (50u * 1000u)          /* ステータス書込  最大30ms  */
#define TIMEOUT_ERASE_4K_US  (400u * 1000u)         /* 4KB消去         最大250ms */
#define TIMEOUT_ERASE_64K_US (1000u * 1000u)        /* 64KB消去        最大700ms */
#define TIMEOUT_CHIP_US      (40u * 1000u * 1000u)  /* 全消去          最大30秒  */
#define TIMEOUT_PREV_US      (1000u * 1000u)        /* 前の処理が終わるのを待つ上限 */

static uint32_t actual_baudrate = 0;

/* ===== CS ====================================================================
 * ICは、CSがLOWの間に届いたバイトを、ひとつながりの命令として受け取ります。
 * そして、CSがHIGHに戻った瞬間に、その命令を実行します。
 */
static inline void cs_low(void)  { gpio_put(AT25SF_PIN_CS, 0); }
static inline void cs_high(void) { gpio_put(AT25SF_PIN_CS, 1); }

/** オペコードと24ビットのアドレスを、送る順（上位のバイトから）に並べます。 */
static void make_head(uint8_t head[4], uint8_t op, uint32_t addr)
{
    head[0] = op;
    head[1] = (uint8_t)(addr >> 16);
    head[2] = (uint8_t)(addr >> 8);
    head[3] = (uint8_t)addr;
}

/* ===== 形ごとの共通関数（このファイルの中だけで使う） ====================== */

/** ① [op] */
static void send_op(uint8_t op)
{
    cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, &op, 1);
    cs_high();
}

/** ② [op][← 読む …]
 *  SPIは、1バイト送ると同時に1バイト受け取ります。オペコードを送っている間に
 *  受け取った1バイトは意味のない値なので、spi_write_blocking に捨ててもらい、
 *  その後の len バイトだけを buf に入れます（buf[0] から意味のある値になります）。
 *
 *  11/16の版は、送ると読むを1回（spi_write_read_blocking）で行っていたので、
 *  buf[0] に意味のない値が入り、中身が1つずつ後ろにずれていました。そのため
 *  JEDEC ID を3バイトで読むと、[意味のない値, 1F, 87] となり、IDの照合が通りません。
 *  この形なら、呼ぶ側が「先頭を1つ飛ばす」ことを覚えておかなくて済みます。
 */
static void send_op_read(uint8_t op, uint8_t *buf, size_t len)
{
    cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, &op, 1);
    spi_read_blocking(AT25SF_SPI_PORT, 0x00, buf, len);
    cs_high();
}

/** ③ [op][書く … →] */
static void send_op_write(uint8_t op, const uint8_t *data, size_t len)
{
    cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, &op, 1);
    spi_write_blocking(AT25SF_SPI_PORT, data, len);
    cs_high();
}

/** ④ [op][アドレス×3] */
static void send_op_addr(uint8_t op, uint32_t addr)
{
    uint8_t head[4];
    make_head(head, op, addr);
    cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, head, sizeof(head));
    cs_high();
}

/** ⑤ [op][アドレス×3][書く … →]
 *  先頭の4バイトとデータを、2回に分けて送っています。CSを上げなければ、
 *  ICから見ると1つの命令です。こうすると、先頭4バイトとデータを1つの配列に
 *  コピーしなくて済みます（260バイトの配列と memcpy が要りません）。
 */
static void send_op_addr_write(uint8_t op, uint32_t addr, const uint8_t *data, size_t len)
{
    uint8_t head[4];
    make_head(head, op, addr);
    cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, head, sizeof(head));
    spi_write_blocking(AT25SF_SPI_PORT, data, len);
    cs_high();
}

/** ⑥ [op][アドレス×3][← 読む …] */
static void send_op_addr_read(uint8_t op, uint32_t addr, uint8_t *buf, size_t len)
{
    uint8_t head[4];
    make_head(head, op, addr);
    cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, head, sizeof(head));
    spi_read_blocking(AT25SF_SPI_PORT, 0x00, buf, len);
    cs_high();
}

/* ===== 初期化と確認 ========================================================= */

AT25SF_result_t AT25SF_init(void)
{
    /* CSを、出力に切り替える前にHIGHにしておきます。
     * 「出力にする → HIGHにする」の順だと、その間の一瞬だけLOWが出て、
     * ICが「命令が始まった」と受け取ってしまうためです。
     */
    gpio_init(AT25SF_PIN_CS);
    gpio_put(AT25SF_PIN_CS, 1);
    gpio_set_dir(AT25SF_PIN_CS, GPIO_OUT);

    /* SPIモード0（クロックは普段LOW、立ち上がりで読む）、上位ビットから。
     * spi_init は、実際に設定できた速さを返します。
     */
    actual_baudrate = spi_init(AT25SF_SPI_PORT, AT25SF_SPI_BAUDRATE);
    spi_set_format(AT25SF_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(AT25SF_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(AT25SF_PIN_SI, GPIO_FUNC_SPI);
    gpio_set_function(AT25SF_PIN_SO, GPIO_FUNC_SPI);

    /* SO（IC → Pico）に、Pico側の弱いプルアップを入れておきます。
     * CSがHIGHの間、ICはSOを切り離す（ハイインピーダンス）ので、線が浮きます。
     * プルアップがあると、ICがつながっていないときに読んだ値が必ず FF になり、
     * 「配線が外れている」ことが、表示から分かりやすくなります。
     * ICが線を動かしているときは、ICの方がずっと強いので、通信には影響しません。
     */
    gpio_pull_up(AT25SF_PIN_SO);

    /* 最初にIDを読んで、通信できているかを確かめます。
     * ここで分かれば、後の「消去が始まらない」「読んだ値がおかしい」の原因を、
     * 配線や速さの問題と、プログラムの問題に切り分けられます。
     */
    return AT25SF_check_id();
}

void AT25SF_read_id(uint8_t id[3])
{
    send_op_read(OP_JEDEC_ID, id, 3);
}

AT25SF_result_t AT25SF_check_id(void)
{
    uint8_t id[3];
    AT25SF_read_id(id);
    if (id[0] != AT25SF_ID_BYTE0 || id[1] != AT25SF_ID_BYTE1 || id[2] != AT25SF_ID_BYTE2) {
        return AT25SF_ERR_ID;
    }
    return AT25SF_OK;
}

uint32_t AT25SF_get_baudrate(void)
{
    return actual_baudrate;
}

const char *AT25SF_result_name(AT25SF_result_t r)
{
    switch (r) {
    case AT25SF_OK:              return "OK";
    case AT25SF_ERR_ID:          return "ERR_ID (JEDEC IDが違う)";
    case AT25SF_ERR_BUSY:        return "ERR_BUSY (前の処理が終わっていない)";
    case AT25SF_ERR_NO_WEL:      return "ERR_NO_WEL (06hの後にWELが1にならない)";
    case AT25SF_ERR_PROTECTED:   return "ERR_PROTECTED (保護ビットが立っている)";
    case AT25SF_ERR_NOT_STARTED: return "ERR_NOT_STARTED (命令の後にBUSYにならない)";
    case AT25SF_ERR_TIMEOUT:     return "ERR_TIMEOUT (BUSYが外れない)";
    case AT25SF_ERR_PARAM:       return "ERR_PARAM (引数が範囲外)";
    }
    return "?";
}

/* ===== ステータス =========================================================== */

uint8_t AT25SF_read_status1(void)
{
    uint8_t s;
    send_op_read(OP_READ_STATUS1, &s, 1);
    return s;
}

uint8_t AT25SF_read_status2(void)
{
    uint8_t s;
    send_op_read(OP_READ_STATUS2, &s, 1);
    return s;
}

bool AT25SF_is_busy(void)
{
    return (AT25SF_read_status1() & AT25SF_SR1_BUSY) != 0;
}

AT25SF_result_t AT25SF_wait_ready(uint32_t timeout_us)
{
    uint64_t start = time_us_64();
    while (AT25SF_is_busy()) {
        if (time_us_64() - start > timeout_us) {
            return AT25SF_ERR_TIMEOUT;
        }
    }
    return AT25SF_OK;
}

void AT25SF_write_enable(void)
{
    send_op(OP_WRITE_ENABLE);
}

/** 06h を送り、本当に WEL が 1 になったかを確かめます。
 *  書き込みと消去は、直前に WEL が 1 でないと無視されます。しかも WEL は、
 *  書き込みや消去が終わるたびに自動で 0 に戻るので、毎回この手順が必要です。
 *
 *  ここで読んだステータスで、保護ビットも一緒に確かめます（読むのは1回で済みます）。
 *  保護された範囲への書き込みや消去は、ICが黙って無視します。そのうえ、無視した後は
 *  BUSYにもならず WEL も0に戻るので、「すぐに終わった書き込み」と見分けがつきません。
 *  送る前に止めて、原因をはっきり返します。
 *  allow_protected は、保護を解除する命令（AT25SF_unprotect）を送るときだけ true にします。
 */
static AT25SF_result_t write_enable_checked(bool allow_protected)
{
    send_op(OP_WRITE_ENABLE);
    uint8_t s = AT25SF_read_status1();
    if (s & AT25SF_SR1_BUSY) {
        return AT25SF_ERR_BUSY; /* BUSYの間は、06h も無視される */
    }
    if (!(s & AT25SF_SR1_WEL)) {
        return AT25SF_ERR_NO_WEL;
    }
    if (!allow_protected && (s & AT25SF_SR1_BP_MASK)) {
        return AT25SF_ERR_PROTECTED;
    }
    return AT25SF_OK;
}

/* ===== 読み出し ============================================================= */

AT25SF_result_t AT25SF_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (buf == NULL || addr >= AT25SF_TOTAL_SIZE || len > AT25SF_TOTAL_SIZE - addr) {
        return AT25SF_ERR_PARAM;
    }
    if (len == 0) {
        return AT25SF_OK;
    }
    /* 書き込み・消去の最中に読むと、正しい値が返りません。
     * 気づかないまま「読んだ値がおかしい」と悩まないように、ここで止めます。
     */
    if (AT25SF_is_busy()) {
        return AT25SF_ERR_BUSY;
    }
    send_op_addr_read(OP_READ_DATA, addr, buf, len);
    return AT25SF_OK;
}

/* ===== 書き込み ============================================================= */

AT25SF_result_t AT25SF_page_program_start(uint32_t addr, const uint8_t *data, size_t len)
{
    /* ページの境界をまたぐと、同じページの先頭に戻って上書きされてしまいます。
     * 送る前に、ここで止めます。(addr & 0xFF) は、ページの中での位置です。
     */
    if (data == NULL || len == 0 || addr >= AT25SF_TOTAL_SIZE ||
        (addr & (AT25SF_PAGE_SIZE - 1)) + len > AT25SF_PAGE_SIZE) {
        return AT25SF_ERR_PARAM;
    }
    AT25SF_result_t r = write_enable_checked(false);
    if (r != AT25SF_OK) {
        return r;
    }
    send_op_addr_write(OP_PAGE_PROGRAM, addr, data, len);
    return AT25SF_OK; /* CSを上げた瞬間に、ICの中で書き込みが始まっている */
}

AT25SF_result_t AT25SF_page_program(uint32_t addr, const uint8_t *data, size_t len)
{
    /* 「待つ → 許可 → 命令 → 待つ」の順です。
     * 前の処理が終わっていないと、次の命令は無視されるので、先に待ちます。
     */
    AT25SF_result_t r = AT25SF_wait_ready(TIMEOUT_PREV_US);
    if (r != AT25SF_OK) {
        return r;
    }
    r = AT25SF_page_program_start(addr, data, len);
    if (r != AT25SF_OK) {
        return r;
    }
    bool started = AT25SF_is_busy();
    r = AT25SF_wait_ready(TIMEOUT_PAGE_US);
    if (r != AT25SF_OK) {
        return r;
    }
    /* 一度もBUSYにならず、WELも1のまま残っていたら、命令は受け付けられていません
     * （受け付けられた書き込みは、終わるとWELを0に戻します）。
     * 数バイトの書き込みは数十µsで終わり、BUSYを見る前に終わることがあるので、
     * WELと合わせて判定します。
     */
    if (!started && (AT25SF_read_status1() & AT25SF_SR1_WEL)) {
        return AT25SF_ERR_NOT_STARTED;
    }
    return AT25SF_OK;
}

/* ===== 消去 ================================================================= */

/** 3種類の消去に共通の手順（待つ → 許可 → 命令 → 待つ）。違うのは命令と上限時間だけ。 */
static AT25SF_result_t erase_common(uint8_t op, bool with_addr, uint32_t addr, uint32_t timeout_us)
{
    AT25SF_result_t r = AT25SF_wait_ready(TIMEOUT_PREV_US);
    if (r != AT25SF_OK) {
        return r;
    }
    r = write_enable_checked(false);
    if (r != AT25SF_OK) {
        return r;
    }
    if (with_addr) {
        send_op_addr(op, addr);
    } else {
        send_op(op);
    }
    /* 消去は、短くても数十msかかります。命令の直後にBUSYになっていなければ、
     * 受け付けられていません（命令が届いていない、ステータスが正しく読めていない、など）。
     * これを確かめないと、「消したつもりで消えていない」まま先へ進んでしまいます。
     * 11/13の実機の結果（全消去時間がすぐに出る）は、この確認に引っかかる状態です。
     * どこで止まったかは、main.c の自己診断の表示で切り分けます。
     */
    if (!AT25SF_is_busy()) {
        return AT25SF_ERR_NOT_STARTED;
    }
    return AT25SF_wait_ready(timeout_us);
}

AT25SF_result_t AT25SF_erase_sector(uint32_t addr)
{
    if (addr >= AT25SF_TOTAL_SIZE) {
        return AT25SF_ERR_PARAM;
    }
    return erase_common(OP_ERASE_4K, true, addr, TIMEOUT_ERASE_4K_US);
}

AT25SF_result_t AT25SF_erase_block(uint32_t addr)
{
    if (addr >= AT25SF_TOTAL_SIZE) {
        return AT25SF_ERR_PARAM;
    }
    return erase_common(OP_ERASE_64K, true, addr, TIMEOUT_ERASE_64K_US);
}

AT25SF_result_t AT25SF_erase_chip(void)
{
    return erase_common(OP_ERASE_CHIP, false, 0, TIMEOUT_CHIP_US);
}

/* ===== 保護の解除 =========================================================== */

AT25SF_result_t AT25SF_unprotect(void)
{
    AT25SF_result_t r = AT25SF_wait_ready(TIMEOUT_PREV_US);
    if (r != AT25SF_OK) {
        return r;
    }
    r = write_enable_checked(true); /* 保護されていても送る（解除するための命令なので） */
    if (r != AT25SF_OK) {
        return r;
    }
    const uint8_t value = 0x00; /* BP0〜BP4 と SRP0 を全部 0 に */
    send_op_write(OP_WRITE_STATUS1, &value, 1);
    r = AT25SF_wait_ready(TIMEOUT_WRSR_US);
    if (r != AT25SF_OK) {
        return r;
    }
    /* 書けたかどうかを、読み戻して確かめます。 */
    if (AT25SF_read_status1() & AT25SF_SR1_BP_MASK) {
        return AT25SF_ERR_NOT_STARTED;
    }
    return AT25SF_OK;
}
