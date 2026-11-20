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
 *
 * ■ 11/16の実機テストの後で直した所
 *   実機で、64KBの消去の次の消去が ERR_BUSY で止まりました（始めてから約20ms後）。
 *   「終わった」と読めた直後に、ICが「まだ処理中」と答えた、ということです。
 *   どちらの読み取りが違っていたのかは、1回の結果からは決められません。
 *   そこで、どちらの場合でも先へ進めるように、次の3つを直しました。
 *     (1) CSをHIGHに戻すときの時間を確保する            → cs_high
 *     (2) 「終わった」は、2回続けて読めたときだけにする  → wait_bits_clear
 *     (3) 待ち終えた直後に BUSY=1 と読めたら、待ち直す    → ready_and_enable
 *   あわせて、起きた回数と、そのとき読んだ値を数えるようにしました（AT25SF_get_diag）。
 *   数が 0 でなければ、どちらが起きたのかを、値から見分けられます。
 */
#include "at25sf.h"

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

/* CSをHIGHにした後、そのまま待つ時間 [µs]。理由は、下の cs_high の説明にあります。 */
#define CS_HIGH_WAIT_US 2u

static uint32_t actual_baudrate = 0;

/* 調子を見るための数です。このファイルの関数は main の側からしか呼ばないので、
 * 割り込みと取り合いになることはなく、volatile は要りません。
 */
static AT25SF_diag_t diag;

/** エラーを返すときに、判定に使った SR1 の値を覚えておきます（後で表示して、原因を探すため）。 */
static AT25SF_result_t fail(AT25SF_result_t r, uint8_t sr1)
{
    diag.error_sr1 = sr1;
    return r;
}

/* ===== CS ====================================================================
 * ICは、CSがLOWの間に届いたバイトを、ひとつながりの命令として受け取ります。
 * そして、CSがHIGHに戻った瞬間に、その命令を実行します。
 *
 * CSをHIGHに戻すときに、2つのことをしています。
 *
 * (1) SPIが最後のビットを送り終えるのを待ってから、HIGHにする
 *     SDKの spi_read_blocking は、最後の1バイトを受け取った時点で戻ってきます。
 *     送り終わるのを待ってから戻る spi_write_blocking とは、ここが違います
 *     （SDKのソース hardware_spi/spi.c で確かめました）。
 *     受け取った時点では、クロックの最後の1周期がまだ終わっていないことがあるので、
 *     待たずにHIGHにすると、クロックが動いている途中でCSが上がります。
 *
 * (2) HIGHにした後、1µs以上そのままにする
 *     ICは、CSがHIGHになったのを見て「命令が終わった」と判断します。
 *     必要な時間は、データシートでは20ns以上です（13.3 の tCSH）。
 *     最適化したビルドでは、cs_high() と次の cs_low() の間が数命令（数十ns）しか
 *     離れません（ビルドした機械語で確かめました）。計算上は足りる値ですが、配線が長いと
 *     波形がなまり、ICに届くHIGHの時間はこれより短くなります。ICがHIGHを見落とすと、
 *     2つの命令が1つにつながり、後ろの命令（ステータスの読み出しなど）に答えが返りません。
 *     余裕が小さいので、1µs以上に広げました。
 *
 *     busy_wait_us_32(2) と、2 を指定しているのは、この関数が「1µs刻みの時計が、
 *     指定した数だけ進むまで」待つためです。1 だと、時計が進む直前に呼んだとき、
 *     ほとんど待たずに戻ります。2 なら、短くても1µsになります。
 *     1ページを書くのに使う命令は3〜4個なので、増える時間は合わせて10µsほどです。
 */
static inline void cs_low(void) { gpio_put(AT25SF_PIN_CS, 0); }

static void cs_high(void)
{
    while (spi_is_busy(AT25SF_SPI_PORT)) {
        tight_loop_contents(); /* (1) 最後のビットを送り終えるのを待つ */
    }
    gpio_put(AT25SF_PIN_CS, 1);
    busy_wait_us_32(CS_HIGH_WAIT_US); /* (2) HIGHのまま待つ */
}

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

AT25SF_write_state_t AT25SF_write_state(void)
{
    /* 読むのは1回です。同じ1回の値から、BUSY と WEL の両方を判定します。
     * 2回に分けて読むと、その間にICの状態が変わり、つじつまの合わない組み合わせになります。
     */
    const uint8_t s = AT25SF_read_status1();
    if (s & AT25SF_SR1_BUSY) {
        return AT25SF_WRITE_BUSY;
    }
    if (s & AT25SF_SR1_WEL) {
        return AT25SF_WRITE_NOT_DONE;
    }
    return AT25SF_WRITE_DONE;
}

/* ===== 終わるのを待つ ========================================================
 * SR1 の mask のビットが全部 0 と、「2回続けて」読めるまで待ちます。
 *
 * 2回にしているのは、1回の読み取りを、そのまま信じないためです。
 * 「BUSY=0 と1回読めたら終わり」という作りだと、読み間違いが1回あっただけで、
 * 処理の途中で先へ進んでしまいます（11/16の実機の結果は、これでも説明がつきます。
 * ただし、原因がこれだと決まったわけではありません）。
 * 増える時間は、ステータスを1回読む分（1MHzで約20µs）です。
 */
static AT25SF_result_t wait_bits_clear(uint8_t mask, uint32_t timeout_us)
{
    const uint64_t start = time_us_64();
    unsigned clear_count = 0;     /* mask のビットが全部 0 と、続けて読めた回数 */
    bool     idle_before = false; /* 1つ前に読んだとき、BUSY=0 だった           */
    uint8_t  idle_sr1 = 0;        /* そのときの SR1                             */

    for (;;) {
        const uint8_t s = AT25SF_read_status1();

        if (s & AT25SF_SR1_BUSY) {
            if (idle_before) {
                /* さっきは BUSY=0 と読めたのに、今度は BUSY=1 です。
                 * どちらかが読み間違いか、ICの中の状態が行き来したかです。数えておきます。
                 */
                diag.busy_again++;
                diag.busy_again_sr1 = idle_sr1;
            }
            idle_before = false;
        } else {
            idle_before = true;
            idle_sr1 = s;
        }

        if ((s & mask) == 0) {
            clear_count++;
            if (clear_count >= 2) {
                return AT25SF_OK;
            }
            continue; /* 1回目。時間切れを見る前に、すぐもう1回読む */
        }
        clear_count = 0;

        if (time_us_64() - start > timeout_us) {
            /* BUSY は 0 なのに WEL が 1 のまま残っているときは、命令が実行されていません
             * （実行された書き込みや消去は、終わると WEL を 0 に戻します）。
             */
            return fail((s & AT25SF_SR1_BUSY) ? AT25SF_ERR_TIMEOUT : AT25SF_ERR_NOT_STARTED, s);
        }
    }
}

/** BUSY が外れるまで待ちます（前の処理が残っていないかを確かめるときに使います）。 */
AT25SF_result_t AT25SF_wait_ready(uint32_t timeout_us)
{
    return wait_bits_clear(AT25SF_SR1_BUSY, timeout_us);
}

/** 書き込み・消去の命令を送った後、それが終わるまで待ちます。
 *  BUSY だけでなく、WEL も 0 に戻ったことを見ます。ICは、書き込みや消去が終わると
 *  WEL を 0 に戻すので（データシート 11.1.3）、2つのビットを合わせて見ると、
 *  「途中なのに BUSY=0 と読めただけ」の場合を、見分けやすくなります。
 */
static AT25SF_result_t wait_done(uint32_t timeout_us)
{
    return wait_bits_clear(AT25SF_SR1_BUSY | AT25SF_SR1_WEL, timeout_us);
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
    const uint8_t s = AT25SF_read_status1();
    if (s & AT25SF_SR1_BUSY) {
        /* BUSYの間は、06h も無視されます。
         * この関数は「前の処理が終わった」と分かってから呼ぶので、ここに来るのは、
         * その判断か、今の読み取りの、どちらかが違っていたときです。
         * 読んだ値が FF なら、ICが答えていません（線が浮いて、プルアップの値が読めている）。
         * 03 なら、ICは本当に処理中です（前の「終わった」が早すぎた）。
         */
        diag.wren_busy++;
        diag.wren_busy_sr1 = s;
        return fail(AT25SF_ERR_BUSY, s);
    }
    if (!(s & AT25SF_SR1_WEL)) {
        return fail(AT25SF_ERR_NO_WEL, s);
    }
    if (!allow_protected && (s & AT25SF_SR1_BP_MASK)) {
        return fail(AT25SF_ERR_PROTECTED, s);
    }
    return AT25SF_OK;
}

/** 「待つ → 許可」をまとめた手順です。終わるまで待つ形の書き込み・消去・保護の解除で使います。
 *
 *  待ち終えた直後なのに、許可の確認で BUSY=1 と読めたときは、エラーにせず、
 *  待つところからやり直します。ICが「まだ処理中」と答えているなら、待てば済むからです。
 *  やり直しは TIMEOUT_PREV_US までで、それでもだめなら AT25SF_ERR_BUSY を返します。
 *  （待たない形の AT25SF_page_program_start は、ここを通りません。あちらは
 *    AT25SF_ERR_BUSY をそのまま返し、呼ぶ側の flash_task が、次の周でやり直します）
 */
static AT25SF_result_t ready_and_enable(bool allow_protected)
{
    const uint64_t start = time_us_64();
    for (;;) {
        AT25SF_result_t r = AT25SF_wait_ready(TIMEOUT_PREV_US);
        if (r != AT25SF_OK) {
            return r;
        }
        r = write_enable_checked(allow_protected);
        if (r != AT25SF_ERR_BUSY) {
            return r; /* OK も、BUSY以外のエラーも、そのまま返す */
        }
        if (time_us_64() - start > TIMEOUT_PREV_US) {
            return r;
        }
    }
}

void AT25SF_get_diag(AT25SF_diag_t *out)
{
    if (out != NULL) {
        *out = diag;
    }
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
     * BUSY=1 と読めたときは、もう1回読んで確かめます（1回の読み間違いで止まらないため）。
     */
    uint8_t s = AT25SF_read_status1();
    if (s & AT25SF_SR1_BUSY) {
        s = AT25SF_read_status1();
        if (s & AT25SF_SR1_BUSY) {
            return fail(AT25SF_ERR_BUSY, s);
        }
    }
    send_op_addr_read(OP_READ_DATA, addr, buf, len);
    return AT25SF_OK;
}

/* ===== 書き込み ============================================================= */

/** ページ書き込みの引数を確かめます。
 *  ページの境界をまたぐと、同じページの先頭に戻って上書きされてしまいます。
 *  送る前に、ここで止めます。(addr & 0xFF) は、ページの中での位置です。
 */
static bool page_param_ok(uint32_t addr, const uint8_t *data, size_t len)
{
    return data != NULL && len != 0 && addr < AT25SF_TOTAL_SIZE &&
           (addr & (AT25SF_PAGE_SIZE - 1)) + len <= AT25SF_PAGE_SIZE;
}

AT25SF_result_t AT25SF_page_program_start(uint32_t addr, const uint8_t *data, size_t len)
{
    if (!page_param_ok(addr, data, len)) {
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
    if (!page_param_ok(addr, data, len)) {
        return AT25SF_ERR_PARAM;
    }
    /* 「待つ → 許可 → 命令 → 待つ」の順です。
     * 前の処理が終わっていないと、次の命令は無視されるので、先に待ちます。
     */
    AT25SF_result_t r = ready_and_enable(false);
    if (r != AT25SF_OK) {
        return r;
    }
    send_op_addr_write(OP_PAGE_PROGRAM, addr, data, len);
    /* 書き終わると、BUSY も WEL も 0 になります。
     * 命令が受け付けられなかったときは、BUSY にならず、WEL が 1 のまま残ります。
     * その場合は、上限時間の後に AT25SF_ERR_NOT_STARTED が返ります。
     */
    return wait_done(TIMEOUT_PAGE_US);
}

/* ===== 消去 ================================================================= */

/** 3種類の消去に共通の手順（待つ → 許可 → 命令 → 待つ）。違うのは命令と上限時間だけ。 */
static AT25SF_result_t erase_common(uint8_t op, bool with_addr, uint32_t addr, uint32_t timeout_us)
{
    AT25SF_result_t r = ready_and_enable(false);
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
     * BUSY=0 と読めたときは、もう1回読んで確かめます（1回の読み間違いで止まらないため）。
     */
    uint8_t s = AT25SF_read_status1();
    if (!(s & AT25SF_SR1_BUSY)) {
        s = AT25SF_read_status1();
        if (!(s & AT25SF_SR1_BUSY)) {
            return fail(AT25SF_ERR_NOT_STARTED, s);
        }
    }
    return wait_done(timeout_us);
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
    AT25SF_result_t r = ready_and_enable(true); /* 保護されていても送る（解除するための命令なので） */
    if (r != AT25SF_OK) {
        return r;
    }
    const uint8_t value = 0x00; /* BP0〜BP4 と SRP0 を全部 0 に */
    send_op_write(OP_WRITE_STATUS1, &value, 1);
    r = wait_done(TIMEOUT_WRSR_US);
    if (r != AT25SF_OK) {
        return r;
    }
    /* 書けたかどうかを、読み戻して確かめます。 */
    const uint8_t s = AT25SF_read_status1();
    if (s & AT25SF_SR1_BP_MASK) {
        return fail(AT25SF_ERR_NOT_STARTED, s);
    }
    return AT25SF_OK;
}
