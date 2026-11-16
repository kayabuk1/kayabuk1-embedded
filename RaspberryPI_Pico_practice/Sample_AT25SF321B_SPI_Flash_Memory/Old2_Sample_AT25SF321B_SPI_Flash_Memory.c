
// ◆2026/11/16コード内の古いコード整理や削除の為、
// Old~.cファイルを作成して。そちらに古いコードを残し、
// 現在の.cファイルから古いコード部分を削除する。
// ◆2026年11月12日考えメモとりあえずバフに書き込む処理はある程度書けた
// 次は、256byte 一気にAT25SFに書き込む処理だな。
// ◆他やっておきたいこと
// ●static inline修飾子を付けて、内部でのみ使う関数は、名前の競合の
// 心配がないので、より短く簡潔な名称で使いやすく。例AT25SF_cs_low⇒cs_low
// ●関数の並びがグチャグチャなので、整理する。
// ●ページ単位での処理が可能になったら、セクタ4k単位での書込み処理を実装する
// ◆logデータをそのまま16byte取得ごとにAT25SFへSPI通信するのは、
// オーバヘッドが大きく、マイクロマウス制御に影響を及ぼすため、
// 間にダブルバッファを挟み256byte一気に送信する方針に変更。
// そのためコメントアウト。後ほど削除するか検討。

#include <stdio.h>
#include <stdint.h>
#include<string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "buzzer/buzzer.h"
#include "pico_led_sw/pico_led_sw.h"
#include <time.h>

#include "pico/rand.h"

// ========= AT25SF321B(ﾌﾗｯｼｭﾒﾓﾘ)配線表（ピン割り当て表）=====================
// -－ピン割り当て表
// *　１｜!CS(ﾁｯﾌﾟｾﾚｸﾄ) ⇒ GPIO3 ※本番GPIO13
// *　２｜SO(IO1)(SerialOutput)(MISO) ⇒ GPIO4 ※本番GPIO12
// *　３｜!WP(IO2)(WriteProtect)書込み保護、内部ﾌﾟﾙｱｯﾌﾟ在り⇒フローティングに
// *　４｜GND
// ‐‐‐―――折り返し
// *　５｜SI(IO0)(SreialInput)(MOSI) ⇒ GPIO7 ※本番GPIO11
// *　６｜SCLK ⇒ GPIO6 ※本番GPIO10
// *　７｜!HOLD(IO3)通信一時停止、内部ﾌﾟﾙｱｯﾌﾟ在り⇒フローティングに
// *　８｜VCC

#define PICO2350 0

#if PICO2350 
#define AT25SF_SPI_PORT spi1
#define AT25SF_PIN_SCK 10
#define AT25SF_PIN_SI  11
#define AT25SF_PIN_SO  12
#define AT25SF_PIN_CS  13
#else
#define AT25SF_SPI_PORT spi0
#define AT25SF_PIN_SCK 6
#define AT25SF_PIN_SO  4
#define AT25SF_PIN_SI  7
#define AT25SF_PIN_CS  3 
#endif
#define AT25SF_SPI_BAUDRATE   (10 * 1000 * 1000)
// RP2040 のSPIマスター最大速度
// 標準システムクロック 125 MHz の場合は、その半分の 62.5 MHz（正確には分周比2で 62.5 MHz）  
// PL022 SPIコントローラの仕様上、マスターモードでは周辺クロック（SSPCLK）
// に対するシリアルクロック（SSPCLKOUT）の最小分周比（CPSDVSR）が
// 偶数の「2」であるため、周辺クロックの半分が上限。
// AT25SF321Bは通常SPIで最大 108 MHz（3.0V〜3.6V時は最大 133 MHz）まで対応
/*原文：
For example, at the maximum SSPCLK (clk_peri) frequency on RP2040 of 133MHz, the maximum peak bit rate in master mode is 62.5Mbps. This is achieved with the SSPCPSR register programmed with a value of 2, and the SCR[7:0] field in the SSPCR0 register programmed with a value of 0.
The ratio of minimum SSPCLK frequency to SSPCLKOUT maximum frequency in the case of the slave mode is 12, and for the master mode, it is two.
 F_SSPCLK(min) >= 2 * F_SSPCLKOUT(max) for master mode
*/
// 配線環境（物理面）の限界（一般知識）
//  ・ブレッドボード／長いジャンパ線：配線の浮遊容量やインダクタンスによる
// 波形なまり・反射が起きるため、10MHz〜15MHz 程度が無難。
// ⇒結論：SPI周波数 10MHzでテストを行う。

/*____________________________________________________
◆AT25SF321B 主要ステータスレジスタビット一覧
ビット記号|所属レジスタ|ビット位置|R/W|機能および判定条件
______________________________________________________
・RDY/BSY|Status Register 1|Bit 0|R|
    Ready/Busyフラグ 0: 準備完了（Ready） 1: 内部書き込み/消去動作中（Busy）
・WEL|Status Register 1|Bit 1|R|
    Write Enable Latchフラグ 0: 書き込み禁止（初期値） 1: 書き込み許可状態
・BP0〜BP4|Status Register 1|Bit 2〜6|R/W|
    ブロックプロテクト メモリ空間の保護領域をソフトウェアで設定。
・QE|Status Register 2|Bit 1|R/W|
    Quad Enableフラグ 0: Normal SPI (WP#/HOLD#動作)
     1: Quad-SPI有効 (IO2/IO3動作)
・P_SUS / E_SUS|Status Register 2|Bit 2 / 7|R|
    Suspendフラグ 1: プログラム/消去処理がサスペンド（一時中断）中。
・CMP|Status Register 2|Bit 6|R/W|
    Complement Block Protectフラグ 0: 通常のプロテクト設定
     1: BP0〜BP4で指定した保護領域を反転
__________________________________________________________________
*/
// =====送信オペコード定義======================================
// uint8_t opecode = NULL;
#define JEDEC_ID_READ 0x9f //ベンダID、メモリ種類、メモリ容量。テストに。
// 1Fはメーカーの番号、87 01はAT25SF321Bを表すデバイスの番号
// ---読み出しオペコード
#define AT25SF_NORMAL_READ 0x03 //1線データ読出し。最大55MHz
#define AT25SF_FAST_READ 0x0B //最大133MHz。ﾀﾞﾐｰ８ｸﾛｯｸが挟まる。
#define AT25SF_READ_SFDP 0x5A //デバイス機能情報表示
// ----制御系オペコード
#define AT25SF_WRITE_ENABLE 0x06 //ｽﾃｰﾀｽﾚｼﾞｽﾀWELを 1 に(書込/消去許可)
#define AT25SF_WRITE_DISABLE 0x04 //WELﾋﾞｯﾄを 0 にして書込/消去禁止
#define AT25SF_ENABLE_RESET 0x50 //初期状態へﾘｾｯﾄ許可1段階目
#define AT25SF_RESET_DEVICE 0x66  //ﾘｾｯﾄ許可2段階目。約30us掛かる。
#define AT25SF_DEEP_POWER_DOWN 0xB9   //超低消費電力モード13uA⇒1uAに
#define AT25SF_RELEASE_POWER_DOWN 0xAB //超低消費電力モード解除
//-----書込み、消去オペコード
#define AT25SF_PAGE_PROGRAM 0X02 //0.4ms:1byte~256byte(1ﾍﾟｰｼﾞ)の範囲内で書込み
#define AT25SF_BLOCK_ERASE_4KB 0X20 //55ms:4kB最小ﾌﾞﾛｯｸ(ｾｸﾀ)単位ﾃﾞｰﾀ消去(全ぶFFに)
#define AT25SF_BLOCK_ERASE_32KB 0X52 //120ms:32kBのﾊｰﾌﾌﾞﾛｯｸ単位で消去
#define AT25SF_BLOCK_ERASE_64KB 0XD8 //200ms:64kBの標準ﾌﾞﾛｯｸ単位で消去
#define AT25SF_CHIP_ERASE_ALL 0XC7 //10秒:メモリ全体を消去
#define AT25SF_SUSPENDE 0X75 //書込み、消去動作を一時中断、ﾌﾞﾛｯｸ読込を優先
#define AT25SF_RESUME 0X7A //中断解除、書込み、消去再開
//------ステータスレジスタ操作オペコード
#define READ_STATUS_REGISTER_1 0X05 //Busy判定、書込消去後のﾎﾟｰﾘﾝｸﾞ処理
#define WRITE_STATUS_REGISTER_1 0X01
#define READ_STATUS_REGISTER_2 0X35 
#define  WRITE_STATUS_REGISTER_2 0X31
#define READ_STATUS_REGISTER_3 0X15
#define  WRITE_STATUS_REGISTER_3 0X11
//================================================================
#define PAGE_WRITE_TAKES_TIME 1000//0.7ms以上完了までに必要 1000us=1ms
#define SECTOR_ERASE_TAKES_TIME 10000 //セクタ消去標準50ms 10ms設定
// ===DEBUG用 printf有効化 走行時 0に===================
#define AT25SF_DEBUG 1
// #if AT25SF_DEBUG
// printf("：%d\n",);
// #endif の様にコード内にprintfを埋め込んで使う。

/* ======各処理に掛かる時間（データシートp.55より）=======
処理内容：データシートの記号表記：標準値(Typ)：最大値(Max)
ページ書き込み（256バイト）：tPP：0.4ms：3.4ms
セクタ消去（4 kBytes）：tBLKE：55ms：250ms
ﾊｰﾌﾌﾞﾛｯｸ消去（32 kBytes）：tBLKE：120ms：450ms
ブロック消去（64 kBytes）：tBLKE：200ms：700ms
チップ全消去（Chip Erase）：tCHIP：10sec：30sec
ステータスレジスタ書込み：tWRSR：5ms：30ms
 ====================================================*/
typedef union{
    uint8_t bytes[256]; //256Bytes (000000h〜0000FFh）
}Page_256B_t; //16384個

Page_256B_t log_page_buf[2] = {0};
// SPIで直接割込み書き込みでは制御に影響が出てしまい、速度的に間に合わない。
// ➡ダブルバッファで、256byte分貯めてから一気に書き込む。
// （書き込んでいる間にもう一つのバッファにデータを書き込む）

typedef union {
    uint8_t bytes[4096];
    Page_256B_t pages[16]; //4096Bytes
}Sector_4kB_t; //1024個

Sector_4kB_t log_sector_buf[2] = {0};
// SPIで直接割込み書き込みでは制御に影響が出てしまい、速度的に間に合わない。
// ➡ダブルバッファで、4kB分貯めてから一気に書き込む。のも試す。
// （書き込んでいる間にもう一つのバッファにデータを書き込む）

typedef union{
    uint8_t bytes[32768];
    Page_256B_t pages[128];
    Sector_4kB_t sectors[8];
} Block_32kB_t; //Block128個

typedef union{
    uint8_t bytes[65536];
    Page_256B_t pages[256];
    Sector_4kB_t sectors[16];
    Block_32kB_t half_blocks[2];
}Block_64kB_t; //block64個

// ◆フラッシュメモリに一回記録するlog_dataの構造
typedef struct log_data
{
    uint8_t sensor_displacement;
    uint8_t sensor_binary;
    uint16_t target_speed;
    uint16_t left_speed;
    uint16_t right_speed;
    int8_t left_motor_duty; //逆回転時にマイナスduty使用するため符号あり
    int8_t right_motor_duty;
    int16_t target_angular_velocity;
    int16_t now_angular_velocity;
    uint16_t vcc_mv;
}Sensors_data_t; //計16byte

#if AT25SF_DEBUG==1
// ダミーデータを設定。
volatile Sensors_data_t now_sensors_data 
= {
    .sensor_displacement = 0x0F,
    .sensor_binary = 0xF0,
    .target_speed = 0xF0F0,
    .left_speed = 0x00FF,
    .right_speed = 0xFF00,
    .left_motor_duty = 0x0F,
    .right_motor_duty = -16, //int8_t なので 0xFF=256は範囲外。負の値でシフト動作確認。
    .target_angular_velocity = -256, //int16_tなので0xFF00では範囲外
    .now_angular_velocity = 0x00FF,
    .vcc_mv = 0X0FF0
};
void pack_random_sensordata();
void pack_random_sensordata(){
    now_sensors_data.sensor_displacement 
    = (uint8_t)get_rand_32();
    now_sensors_data.sensor_binary 
    = (uint8_t)get_rand_32();
    now_sensors_data.target_speed 
    = (uint16_t)get_rand_32();
    now_sensors_data.left_speed 
    =  (uint16_t)get_rand_32();
    now_sensors_data.right_speed 
    = (uint16_t)get_rand_32();
    now_sensors_data.left_motor_duty 
    = (int8_t)get_rand_32();
    now_sensors_data.right_motor_duty 
    = (int8_t)get_rand_32(); 
    //int8_t なので 0xFF=256は範囲外。負の値でシフト動作確認。
    now_sensors_data.target_angular_velocity 
    = (int16_t)get_rand_32(); 
    //int16_tなので0xFF00では範囲外
    now_sensors_data.now_angular_velocity 
    = (int16_t)get_rand_32();
    now_sensors_data.vcc_mv = (uint16_t)get_rand_32();
}
#else
volatile Sensors_data_t now_sensors_data = {0};
#endif

typedef struct {
    uint64_t chip_erase_time;
    uint32_t write_1page_time[1024];
    uint64_t write_count;
    uint64_t read_time;
}mesurement_time_t;
mesurement_time_t mesurement_time = {0};

#if 0 //RP2350は内蔵SRAM 520KB のため、こんな変数は使えない。
// データ構造把握用
typedef union{   
    Block_64kB_t blocks[64];
    Block_32kB_t half_blocks[128];
    Sector_4kB_t sectors[1024];
    Page_256B_t pages[16384];
    uint8_t bytes[4194304];
}AT25SF321B_AllMemory_32Mbit_t;
#endif
// ===グローバル変数========================

// ==========================================
// ------ハードウェアドライバ部分

// -----タイマー割り込みで書込み＆読み出す部分
bool AT25SF_write_timer_start(int64_t intervar_us);
// void AT25SF_read_timer(uint64_t write_memo_addr, );
// 何を渡すか。

// -----タイマー割込みの中で呼ばれる関数や、その他基本関数

//================ 形ごとの共通関数（内部専用）======================
/*
◆関数について整理。
層	役割	主な関数	持たないもの
ドライバ	SPIでICと話す	init（IDの確認）、ステータス、許可、消去、ページ書き込み、読み出し	バッファ、スイッチ
ログ	記録をためて、ページ単位で書く。読み出してCSVで出す	log_pack、log_put、flash_task、消去、ダンプ	スイッチ、ブザー、タイマー
アプリ（main）	いつ何をするかを決める	コマンドの受け付け、タイマー、テストデータ	—
◆コマンドの送り方は、5つの形しかない
データシートのタイミングチャートを並べると、このICへの命令は、
どれも次の5つの形のどれかです。
① [op]                        06h  04h  C7h
② [op][← 読む …]              05h  9Fh
③ [op][アドレス×3]            20h  52h  D8h
④ [op][アドレス×3][書く … →]  02h
⑤ [op][アドレス×3][← 読む …]  03h
==========================================
共通関数	形	今のコードで同じことをしている所
cmd_only	①	AT25SF_write_enable() の中身、AT25SF_send_opecode()、
AT25SF_erase_all() でC7hを送っている3行
cmd_read	②	read_status_register_1()、read_status_is_busy()、
AT25SF_print_id_status()
cmd_addr	③	まだ無い（セクタ消去とブロック消去で使う）
cmd_addr_write	④	log_write_1page() の送信部分、AT25SF_write_byte()
cmd_addr_read	⑤	AT25SF_read() そのもの
wait_ready	②の繰り返し	AT25SF_erase_all() の中の 
while (read_status_is_busy()) のループ
*/
static inline void AT25SF_cs_low(void);
static inline void AT25SF_cs_low(void){
    gpio_put(AT25SF_PIN_CS, 0);
}
static inline void AT25SF_cs_high(void);
static inline void AT25SF_cs_high(void){
    gpio_put(AT25SF_PIN_CS, 1);
}


static void AT25SF_only_send_opecode(uint8_t op);                                 
// 06h, C7h
static void AT25SF_only_read(uint8_t op, uint8_t *buf, size_t len);         
// 05h, 9Fh, 読み込んだデータを入れるbufを渡してそれにデータを入れてもらうので
// 戻り値はvoidにする。
static void AT25SF_send_opecode_addr(uint8_t op, uint32_t addr);                    
// 20h, 52h, D8h(4KB消去、32KB消去、64KB消去)
static void AT25SF_addr_write(
uint8_t op, uint32_t addr, const uint8_t *rx, size_t len);  
// 02h
static void AT25SF_addr_read(
uint8_t op, uint32_t addr, uint8_t *buf, size_t len);         
// 03h
static bool wait_ready(uint32_t timeout_us);   
// BUSYが外れたら true、時間切れなら false

static void AT25SF_only_send_opecode(uint8_t op)
{
        AT25SF_cs_low();    
        spi_write_blocking(AT25SF_SPI_PORT, &op, 1);
        AT25SF_cs_high();
}
static void AT25SF_only_read(uint8_t op, uint8_t *buf, size_t len)
{
    uint8_t tx[len];
    tx[0] = op;
    for (uint8_t i=1; i < len; i++){
       tx[i] = 0x00;
    }
    AT25SF_cs_low();
    spi_write_read_blocking(
           AT25SF_SPI_PORT, tx, buf, len);
    AT25SF_cs_high();
}

static inline void AT25SF_write_enable(void);
static inline void AT25SF_write_enable(void){
    // WEL: WriteEnableLatch
    uint8_t op = AT25SF_WRITE_ENABLE;
    AT25SF_only_send_opecode(op);
    // WriteEnaaleコマンド 06hを送信し、書込み許可BIT(WEL)を１に。
    // WELが１になるのは CS が HIGH になった瞬間。
    // この後、データの書き込み処理などが続き、BUSYBITが０になると、
    // 自動的に WELBIT も ０ み戻る。
}


uint8_t read_status_register_1(void);
/**
 * @brief ステータスレジスタ1を読み出す関数
 * @return 取得したステータスレジスタ1の8bitデータ
 */
uint8_t read_status_register_1(void)
{   
    uint8_t op = READ_STATUS_REGISTER_1;
    uint8_t rx[2] = {0};
    AT25SF_only_read(op, rx, sizeof(rx));
    return rx[1];
}

typedef enum {FLASH_BUSY, FLASH_READY} flash_state_t;
flash_state_t flash_state = FLASH_BUSY;
bool read_status_is_busy(void);
/**
 * @brief ステータスレジスタ1を読み出す関数
 * @return 取得したステータスレジスタ1のBIT0の 0 or 1
 */
bool read_status_is_busy(void)
{
    uint8_t op = READ_STATUS_REGISTER_1;
    uint8_t rx[2] = {0};
    AT25SF_only_read(op, rx, sizeof(rx));
    // opecodeの8バイト後のダミービット8バイト送信して、
    // 8ビットのレジスタ1のデータが返ってくるので、送るデータ数は2。
    // SPIはリングバッファなので、受信するには同じサイズのデータを送っている。
    bool status = rx[1] & 0x01;
    printf("BUSY/REDY：%d\n",status);
    flash_state = !status;
    // なので opecode 送信時にもダミーデータ8bitを受信している。
    // なので、ステータスレジスタが入っているのはrx[1]。
    return status;
    // Ready/BusyBITが 1の時Busy,0の時Ready。
}

// repeating_timer_t * を受け取るタイマーコールバック用アダプター
static bool polling_status_callback(repeating_timer_t *timer)
{
    (void)timer;
    return read_status_is_busy();
}

/**
 * 指定された時間(μs)だけ!RDY/BSYビットが0(!RDY)になるまで待機する関数
 * 渡す時間は130行目付近の各処理に掛かる時間表を参考に。
 */
static bool wait_ready(uint32_t timeout_us)
{
    uint64_t start = time_us_64();
    while(read_status_is_busy()){
        if(time_us_64() - start >= timeout_us) return false;
    }
    return true;
}

/**
 * 
 */
bool AT25SF_print_id_status(void) {
    uint8_t rx_id[3] = {0};
    uint8_t op = JEDEC_ID_READ;
    AT25SF_only_read(op, rx_id, sizeof(rx_id));
    bool AT25SF_init_status =
     (rx_id[0]==0x1f && rx_id[1]==0x87 && rx_id[2]==0x01);
    #if AT25SF_DEBUG
    printf("%02X %02X %02X\n", rx_id[0], rx_id[1], rx_id[2]);
    printf("通信成否: %d\n", AT25SF_init_status);
    #endif
    return AT25SF_init_status;
}

// ===============◆割り込み処理考えメモ==========================
// add_repeating_timer_us()をそのまま使うのでは使いずらい、
// ➡start_polling()みたいな関数でラップしたい。
// start_polling⇒add_repeating_timer_ms⇒polling_timer_callback
// ⇒この中でread_status_is_busy()を実行させる。
// ⚠Readyになったことを誰にも知らせていないのでmainから不明問題。
// ➡read_status_is_busy()自体がboolを返すのでif()の条件として使う。
// ➡状態保持用のデータ、構造体などを用意してそこのフラグを書き換える。
// 今はis_busy-write関数など、とダイレクトにつながっているので、
// is_busy-状態記録バッファ-write関数などにして疎結合にする？
static repeating_timer_t polling_timer;
// ↑add_repeating_timer_us() に渡すタイマー管理用の構造体は、
// タイマーが動いている間ずっとメモリ上に生き残っている必要がある。
bool start_polling(uint64_t interval_us,
bool(*polling_timer_callback)(repeating_timer_t *));
bool start_polling(uint64_t interval_us,
bool(*polling_timer_callback)(repeating_timer_t *))
{
    return add_repeating_timer_us(
        interval_us, polling_timer_callback, NULL, &polling_timer
    );
    // ⚠注意：add_repeating_timer_us に渡す関数は、
    // 「repeating_timer_t * を1つ受け取り、boolを返す」形と決まっている。
}
// main内使用例
//     start_polling(PAGE_WRITE_TAKES_TIME, polling_status_callback);
// ◆ポーリング処理考えメモ
// ●ポーリング間隔はどの程度が良いか？根拠が分かる部分はある？
// ➡データシートの「Page Program Time（標準約0.7ms）」や
// 「Sector Erase Time（標準約50ms）」が目安。
// マイコンが全速力でSPIを回し続けるのを防ぐため、
// 書き込みなら100μs〜1ms、消去なら1ms〜10ms程度のウェイトを入れるのが現実的。
// ●0x05を送れば、自動的にステータスビットのBIT0Ready/Busyフラグが読み取れる？
// ➡Cから返ってくるのはステータスレジスタ全体の「8ビット（1バイト）」。
// 自動でBit0だけにはならない。
// ●それとも受けっとった8bitのデータから自分でビットシフト等で取り出す必要ある？
// ●それなら関数名もread_status_is_busy();とかに変えようかな。
// ●何かの処理ではCSLOWにした後に、通信が終わったら自動でWELを0に戻してくれた
// 気がしているのだけれど、それは何の処理だったか分かる？
// ➡Page Program（書き込み）やSector Erase（消去）などの内部処理が
// 完了したとき、IC側が自動でWELを 0（書き込み禁止）に戻す。

// ↓基本的にはSPIを通信のいつもの方でオペコードを送信する書き方で
// 良いと思うが、特によく使うもの、明示的に書いて処理を書いておいたほうが
// 良いものは別に関数としてまとめておくのがよいかな。
// 書き込んだら、書き込んだ分だけ、アドレスを進めるカウンターが欲しいな。
// 書込み終わったらカウンタを進める処理をする。
// アドレスは24bitだな丁度よい型がないな。uint8_tを3個、0x00 00 00
// みたいにして増やしていくのが良いのだろうか？
// 全送信データは書込みなら40bit
// ＝＝＝＝＝＝＝＝＝＝割込みについてのメモここまで＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝

static void AT25SF_addr_write(
    uint8_t op, uint32_t addr, const uint8_t *data, size_t len)
{
    uint8_t tx[4] = {0};
    tx[0] = op;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8) & 0xFF;
    tx[3] = addr;
    AT25SF_cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, tx, 4);    
    // 先頭の4バイト
    spi_write_blocking(AT25SF_SPI_PORT, data, len);   
    // 続けてデータ（CSはLOWのまま）
    AT25SF_cs_high();
}

static uint32_t write_memo_addr = 0x00000000; //00000000h~3FFFFFh
static uint32_t read_memo_addr = 0x00000000; //00000000h~3FFFFFh
static uint32_t erase_memo_addr = 0x00000000; //00000000h~3FFFFFh
static uint8_t page_count = 0;
static uint32_t end_memo_address = 0x400000;

void AT25SF_write_bytes_data(uint8_t op, uint8_t data);
void AT25SF_write_bytes_data(uint8_t op, uint8_t data)
{   
    AT25SF_write_enable();
    AT25SF_addr_write(op, write_memo_addr, &data, sizeof(data));
    write_memo_addr += sizeof(data);
    // 未◆：256byteを越えてページが跨ってしまう場合は、ページを変える
    // 処理を書く必要がある。
    /*◆write_memo_addrについて
    ●ページ番号は write_memo_addr >> 8
    ●ページ内の位置は write_memo_addr & 0xFF
    ●セクタ番号は write_memo_addr >> 12 で計算できるのはなぜか。
    ===================================================
    10進数で考えると
    1ページに100件書ける帳簿で、通し番号12345番は「123ページ目の45番」。
    ページ番号は100で割った商で、下2桁を捨てた数。
    ページ内の位置は余りで、下2桁だけを残した数。
    2進数でも同じ。
    1ページは256バイト、つまり2の8乗バイト。
    256で割る ＝ 下8ビットを捨てる ＝ >> 8 ＝ ページ番号
    256で割った余り ＝ 下8ビットだけ残す ＝ & 0xFF
    （0xFF は、下8ビットだけが1のマスク）＝ページ内の位置（アドレス）
    16進数の1桁は4ビットなので、8ビットは16進数の下2桁。
    セクタは4096バイト、つまり2の12乗バイト。
    なので、>> 12 がセクタ番号、& 0xFFF がセクタ内の位置になる。
    例：アドレス 0x012345
    知りたいもの	計算	結果
    ページ番号	0x012345 >> 8	0x0123（291）
    ページ内の位置	0x012345 & 0xFF	0x45（69）
    セクタ番号	0x012345 >> 12	0x012（18）
    セクタ内の位置	0x012345 & 0xFFF	0x345
    24ビットのアドレスを図にすると、次のとおり。
    A23 ........ A12 | A11 ... A8 | A7 ........ A0
    <-- セクタ番号 -->|<------ セクタ内の位置 ------>
    <------- ページ番号 --------->|<- ページ内の位置 ->
    データシート「4KBの消去では A11〜A0 を無視する」。
    これは、この図の「セクタ内の位置」の部分。
    どのセクタを消すかは上位のビットだけで決まるので、
    セクタ内のどのアドレスを送っても、同じセクタが消える（下位12ビットは関係ない）。
    64KBの消去なら A15〜A0 を無視するので、>> 16 がブロック番号。
    */ 
}


/*◆変数についてのメモ
変数ごとに「誰が書き、誰が読むか」を決める。
割り込みとmainが関わるコードでは、変数ごとにこれを決めておくと、
迷わなくなる。
　　　　　　　　　　　　　　　　変数	意味	書く人	読む人
total_bytes、now_filling、nth_byte｜割り込みが今どこに詰めているか｜割り込み｜割り込み
buf_state｜どの箱が運ばれる待ちか（札）｜割り込み（付ける）,main（外す）｜両方
write_memo_addr	フラッシュの次の書き込み先	main	main
pack_dropped	あふれた回数	割り込み	main（表示）

mainは now_filling を読む必要がない。
mainが知りたいのは「どの箱を運ぶか」なので、buf_state だけを見れば足りる。
両方から触る変数は、buf_state と pack_dropped 。
この2つに volatile を付ける。
*/

typedef enum{
    NOT_READY = -1,
    BUF_A = 0,
    BUF_B =1
} buf_state_t;
volatile buf_state_t buf_state = NOT_READY;
//書き込みを待っているバッファの番号。無ければ -1
// ➡bufの状態がわかりやすい様にenum作ったのにうまく使えていないな、、、。
volatile static uint pack_dropped = 0; //  // 書込みあふれ。数えておき、このページは捨てる
static uint32_t total_bytes = 0; 
static uint16_t nth_byte = 0; //バッファの中で、次に詰める位置（0〜255）
static uint8_t now_filling = 0; 
//割り込みが今詰めているバッファの番号（0か1）
static void log_pack(uint8_t *dst, const volatile Sensors_data_t *src);
static void log_pack(uint8_t *dst, const volatile Sensors_data_t *src)
// ↑log_page_buf[0~1].bytes[n]どちらのページのどの位置かまで指定して渡す。
// コピーするデータもセンサーデータ構造体が前のデータ用など増えた時に備えて、
// ポインタ汎用的な *src で受け取り、アロー演算子でアクセスする。
// ◆引数についてメモ
// C言語では dst[i]と書いた瞬間にポインタではなく実体と解釈される。
// 内部で、ポインタ[番号] ⇒*(ポインタ + 番号) に変換される。
// dst：配列の先頭アドレス＝ポインタ
// dst+1：次の配列要素のアドレス＝ポインタ
// *(dst + 1)：間接参照*＝その場所にあるデータ実体そのもの。
// それを「別の変数に代入したとき」や「ポインタではない関数の引数に渡したとき」
// コピーが作られる。
// ●引数への値渡しとポインタ渡しについて
// 「.」を使うと実体のコピーが渡され元データ壊れない為安全。
// しかし毎回256Byteのコピーが発生して処理が重くなる。
// 「*」で渡し、「->」で扱う場合はデータの住所(4~8byte程度)のため一瞬で渡せる。
// srcの修飾子について。
// now_sensors_data には「割り込み等で勝手に書き換わる可能性がある
// （volatile）」という目印がついているのに、関数の引数 Sensors_data_t *src
//  に渡すときにその目印を書かないとコンパイルエラーになる。
{   
    uint32_t ints = save_and_disable_interrupts();
    dst[0] = src->sensor_displacement;
    dst[1] = src->sensor_binary;
    dst[2] = (src->target_speed >> 8) & 0xFF;
    dst[3] = src->target_speed & 0xFF;
    dst[4] = (src->left_speed >> 8) & 0xFF;
    dst[5] = src->left_speed & 0xFF;
    dst[6] = (src->right_speed >> 8) & 0xFF;
    dst[7] = src->right_speed & 0xFF;
    dst[8] = src->left_motor_duty;
    dst[9] = src->right_motor_duty;
    dst[10] = ((uint16_t)src->target_angular_velocity >> 8) & 0xFF;
    dst[11] = src->target_angular_velocity & 0xFF;
    dst[12] = ((uint16_t)src->now_angular_velocity >> 8) & 0xFF;
    dst[13] = src->now_angular_velocity & 0xFF;
    dst[14] = (src->vcc_mv >> 8) & 0xFF;
    dst[15] = src->vcc_mv & 0xFF;
    restore_interrupts(ints);

    // ◆呼び出し側で log_pack(
    // &log_page_buf[now_filling].bytes[nth_byte], &now_sensors_data);
    // と、nth_byte 分進めたアドレスを渡しているので、
    // 位置が二重に足される。128バイト目以降は、配列の外を書き換える
    // ➡dst[0]~[15]で良い。
    // ◆思い付きメモ：now_sensors_data のメンバ変数名が異なるので、
    // ループで回すことが出来ない。ループで回す方法はないだろうか？
    // ポインタで受け取り、ポインタをインクリメントする？
  
    // ◆いや、forで回すのでなくて、割り込みで呼ばれることを想定して、
    // 配列の指定を、%16,%256などを使って、staticかグローバルにして管理して、
    // 値の格納処理後に、カウントを進める書き方に変える必要があるかな。
    // それで256byte=1pageバッファに値が溜まったら、startwritelogAT25SF
    // みたいな非同期処理の関数を呼び出してスタートさせる処置みたいに掛けたら
    // 理想だな。
}

// ↓バッファに詰め込む係（割り込み）の中で呼ばれる。
// それとは別に、AT25SFに書き込む係(main内で実行※重いSPI通信使う為)も作る。
void log_put(void);
void log_put(void)
{   
    now_filling = (total_bytes / 256) % 2; //16/256=0%2=0,256/256=1%2=1,272/256=1%2=1,512/256=2%2=0
    nth_byte = total_bytes % 256; //16%256=16,32%256=32...256%256=0
    // if (nth_byte >= 256){
    //     now_filling = now_filling ^ 1;
    // } ifを使った方が分かりやすいかもしれない、、、。
    log_pack(
        &log_page_buf[now_filling].bytes[nth_byte], &now_sensors_data);
    // ↑16byte詰める関数を呼び出しbufに詰める。
    total_bytes += 16;
    // ↑log_pack()は詰めるだけに徹して、total_byte加算はより大きい役割の
    // log_put()内に記述。

    #if AT25SF_DEBUG
    pack_random_sensordata();
    #endif
    // ●メモ：if(nth_byte >= 256) start_write_AT25SF();
    // みたいに256になったら書込み処理をスタートさせたい。
    if(total_bytes % 256 == 0){ //一ページ溜まった瞬間。
        if(buf_state >= 0){ //-1になっていない＝前のページ書込み未完了。
            pack_dropped++; //詰め込みあふれを数え。このページは捨てる。
            total_bytes -= 256; //今のページを捨てて同じページに最初から
            // -256しなくても、バフを切り替えなければ、そのまま上書きされる？
            // ➡次の呼び出しの先頭でnow_filling = (total_bytes / 256) % 2;
            //16/256=0%2=0,256/256=1%2=1,
            // が計算され、now_fillingが切り替わってしまうので －256は必須。
        }else{
            switch (now_filling)
            {
            case BUF_A:
            buf_state = BUF_A;
                break;
            
            case BUF_B:
            buf_state = BUF_B;
                break;
            }
            // buf_state = now_filling;
            // ➡折角enum定義を書いたので、switch文に変えて可読性高くした。
            // now_filling ^= 1;
            // ➡自身との排他的論理和は毎回1になるので間違い。
            // Bが満杯になったとき：1 ^ 1 = 0 
            // → !now_filling は1（B）→ たまたま正しい
            // Aが満杯になったとき：0 ^ 0 = 0 
            // → !now_filling は1（B）→ 間違い
            // つまり、半分のページは偶然うまくいき、
            // 残り半分は間違ったページを書く。
            // ^= 1 にすれば両方正しくなるが、
            // 次の呼び出しの先頭で now_filling は計算し直されるので、
            // この行はそもそも要らなかった。➡消去。

        }
    }
}


/**
 * 256byte=1pageをまとめて書き込む関数。
 * SPIを通信を使いAT25SFに書込む処理で重いためメインループから呼ぶ。
 */
bool log_write_1page(buf_state_t bs);
bool log_write_1page(buf_state_t bs) //変数bsの中が0ならA、1ならB
{
    uint32_t start_time = time_us_32();
    // uint16_t max = (sizeof(Page_256B_t)/2) + 4;
    // uint8_t tx[max] = {0};
    // ⚠↑の書き方は間違い。配列の要素数指定する max が、
    // コンパイル時に決まらず、実行時に決まる普通の変数(可変長配列)
    // になっているためサイズがコンパイル時に分からないため、
    // ={0};で一括に初期化できない。

    AT25SF_addr_write(
        AT25SF_PAGE_PROGRAM, write_memo_addr,
        log_page_buf[bs].bytes, sizeof(log_page_buf[bs].bytes));
    // C言語では「配列名は先頭要素のポインタ（uint8_t *）」として扱われるため、
    // & を付けなくても自動的に256バイトの先頭アドレスが渡される。

    // ◆修正点：log_write_1page が !now_filling のページを書く
    // ページがたまった直後は、次の割り込みまで最大1ms、
    // now_filling がまだ詰め終えたページを指している。
    // そのため !now_filling は古い方のページになる。
    // ※mainループは数μsごとに回る。
    write_memo_addr += sizeof(log_page_buf[bs].bytes);
    uint32_t end_time = time_us_32();
    uint32_t elapsed_time = end_time - start_time;
    mesurement_time.write_1page_time[mesurement_time.write_count]
    = elapsed_time;
    if(mesurement_time.write_count < 1024){
        mesurement_time.write_count++;
    }
    #if AT25SF_DEBUG
    printf("1page書込み時間：%lu μs\n", elapsed_time);
    #endif
    return true;
}

/**
 * opecodeだけでなく、アドレスの指定も必要な命令送信時に使用(ｾｸﾀ、ﾌﾞﾛｯｸ消去)
 */
static void AT25SF_send_opecode_addr(uint8_t op, uint32_t addr)
{
    uint8_t tx[4] = {0};
    tx[0] = op;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8) & 0xFF;
    tx[3] = addr;
    AT25SF_cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, tx, 4);    
    // 先頭の4バイト
    AT25SF_cs_high();
} 

/*
試験の形：セクタ消去 → 書く → バッファを0で消す → 読み戻す、の順です。
全消去（10〜30秒）ではなく4KBの消去（標準55ms）を使うので、何度も試せます。
読み戻す前にバッファを消すのは、RAMに残った値を見てしまわないためです。
Page Programは1回に256バイトまでなので、4KBは256バイト×16回に分けて書きます。
*/
 // 例：4KBのセクタ消去
bool AT25SF_erase_sector(uint32_t addr)
{
    if (!wait_ready(10 * 1000)) return false;              
 // 前の処理が終わるのを待つ(10ms)
 
    AT25SF_write_enable();
    AT25SF_send_opecode_addr(AT25SF_BLOCK_ERASE_4KB, addr);
    erase_memo_addr += sizeof(Sector_4kB_t);

    if (!read_status_is_busy()) return false;  
 // BUSYにならない＝受け付けられていない

    return wait_ready(300 * 1000);                         
 // 最大250ms（p.55）なので300ms
}

// ↓初期化関数の中で呼ぶ全メモリ消去関数
// ➡毎回電源起動時の初期化で呼んでしまうと、ログのデータを読み出そうと電源
// を入れた時にログが消えてしまう。
// ➡SW２を押した時に実行する様に変更。
bool erase_completed_flag = false;
// 全消去完了管理フラグ。※書き込む前に全消去する必要がある。
void AT25SF_erase_all(void);
void AT25SF_erase_all(void){

    uint64_t start_time = time_us_64();
    uint8_t op = AT25SF_CHIP_ERASE_ALL;
    AT25SF_write_enable();
    AT25SF_only_send_opecode(op);
    // CSピンを High に戻した瞬間にIC内部で自動消去が始まる。
    // Chip Erase(全ﾒﾓﾘ領域の初期化※全て0xFF=１になる)
    // 全領域の消去には約10秒程度掛かる見込み
    wait_ready(35*1000*1000);
    // チップ全消去（Chip Erase）：tCHIP：10sec：30sec
    uint64_t end_time = time_us_64();
    uint64_t elapsed_time = end_time - start_time;
    mesurement_time.chip_erase_time = elapsed_time;
    #if AT25SF_DEBUG
    printf("全消去時間：%llu μs\n", elapsed_time);
    #endif

    erase_completed_flag = true;
    for(int i=0; i <1000; i++)
    {
        beepProcess();
        out_led2(1);
        sleep_ms(1);
    }
}


void flash_task(void)   // mainループで毎回呼ぶ。すぐに戻る
{   
    if(erase_completed_flag){
        switch (flash_state) {
        case FLASH_READY:
            if ( buf_state >= 0) {
                if(log_write_1page(buf_state)){
                    // ↑中でlog_write_1page();を呼べ構造にしたらよいかな。
                    // それともそのままlog_write_1page()を実行するのは、
                    // (データ運送係)は main にさせた方が良い？
                    // WREN → 02h+アドレス+256B（送るだけで待たない）
                    // log_write_1page()内最後で+256しているが、どちらで
                    // アドレスを加算するのが良いのだろうか？
                    // ➡log_write_1pageの中だけにすることに。
                    buf_state = NOT_READY;
                    flash_state = FLASH_BUSY;
                }
            }
            break;
        case FLASH_BUSY:
            if (!read_status_is_busy()) {   // 1回確かめるだけ
                flash_state = FLASH_READY;
            }
            break;
        }
    }
}

// ➡20261115 read関数が純粋なreadになっていないので、
// 読み出し処理とPCに転送処理に分離したい。
void AT25SF_addr_read(
    uint8_t op, uint32_t addr, uint8_t *rx, size_t len)
{   
    uint32_t read_address = addr; //0000_0000h~3FFFFFh
    uint8_t tx[4] = {0};
    tx[0] = op;
    tx[1] = (read_address >> 16) & 0xFF;
    tx[2] = (read_address >> 8) & 0xFF;
    tx[3] = read_address;
    AT25SF_cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, tx, 4);
    spi_read_blocking(AT25SF_SPI_PORT, 0x00, rx, len);
    AT25SF_cs_high();
}



// #define AT25SF_NORMAL_READ 0x03 //1線データ読出し。最大55MHz
// #define AT25SF_FAST_READ 0x0B //最大133MHz。ﾀﾞﾐｰ８ｸﾛｯｸが挟まる。
static uint8_t read_buf[4096] = {0};
// 関数の内側（ローカル変数）で uint8_t tx[65536] と 
// rx[65536] を取ると、合計約131KBのスタックを消費する。
// Picoの標準スタック領域（数KB〜数十KB程度）を大きく超えてメモリ破壊を起こす。
// staticを付けて静的領域なら264KB、65535byte=64KB何とか足りる。
// ➡が、やはり他のコードの使用する静的領域なども考えると大きすぎる可能性がある。
// ➡4KB単位か、32KB単位を検討。
void dump_log_to_pc(uint32_t end_addr);
// PCへログを一括転送(ダンプ)する関数

void dump_log_to_pc(uint32_t end_addr)
{ 
    uint8_t r[16];
    printf("=== START DUMP ===\n");
    for (uint32_t addr = 0; addr < end_addr; addr += 16) {
        AT25SF_addr_read(addr, r, 16);
        if (is_all_ff(r, 16)) break;                     
        // 書いていない所まで来た
        uint16_t target_speed = (uint16_t)((r[2] << 8) | r[3]);
        int16_t  target_av    = (int16_t)((r[10] << 8) | r[11]);
        printf("%u,%u,%u,%d\n", r[0], r[1], target_speed, target_av);  
        // 残りの項目も同じ形で足す
    }
    printf("=== END DUMP ===\n");
}

static volatile uint32_t log_count = 0;
// 1. タイマー割り込みで呼ばれるコールバック関数
bool timer_1ms_callback(struct repeating_timer *t) {
    log_put(); // 1msごとにログ詰め込みを実行
    log_count++;
    return log_count < 1024;   
 // 1024件以上になったらfalseを返してタイマ割込みを止める
 // ＝ちょうど64ページで止まる
}


bool AT25SF_init(void); 
//この名前だと最初の初期化処理もするみたいに見えるな

bool AT25SF_init(void) {
    spi_init(AT25SF_SPI_PORT, AT25SF_SPI_BAUDRATE);
    spi_set_format(
        AT25SF_SPI_PORT, 8, SPI_CPOL_0
        , SPI_CPHA_0, SPI_MSB_FIRST);
        
    gpio_set_function(AT25SF_PIN_SI, GPIO_FUNC_SPI);
    gpio_set_function(AT25SF_PIN_SO, GPIO_FUNC_SPI);
    gpio_set_function(AT25SF_PIN_SCK,  GPIO_FUNC_SPI);

    gpio_init(AT25SF_PIN_CS);
    gpio_set_dir(AT25SF_PIN_CS, GPIO_OUT);
    gpio_put(AT25SF_PIN_CS, 1);

    return AT25SF_print_id_status();
 // 初期化処理時に通信テストとして、JEDEC_IDを読み出す処理を行う。
}
void AT25_hardware_setup(void)
{
  gpio_set_function(10, GPIO_FUNC_SIO);
  gpio_set_function(12, GPIO_FUNC_SIO);
  gpio_set_function(13, GPIO_FUNC_SIO);
  gpio_set_function(15, GPIO_FUNC_SIO);
  gpio_set_dir(10, GPIO_OUT);
  gpio_set_dir(12, GPIO_OUT);
 // Picoボード上のSW0の入力を受け付けるための準備
  gpio_set_dir(13, GPIO_IN);
  gpio_set_dir(15, GPIO_IN);
  // SWに接続されているGPIOのpull up抵抗をONに。
  gpio_pull_up(13);
  gpio_pull_up(15);
}

int main()
{
    stdio_init_all();
    AT25_hardware_setup();
    initBeep();
    AT25SF_init();

    struct repeating_timer timer;

    while (true) { 
    int ch = getchar_timeout_us(0);
    // getchar_timeout_us：
    // Raspberry Pi Pico（RP2040 / RP2350）の Raspberry Pi Pico SDK で
    // 提供されている標準入力（stdin）から1文字読み込むための関数。
    // 0 を指定すると、待機せずにバッファ内にある文字を即座に返し、
    // なければ PICO_ERROR_TIMEOUT（通常は -1）を返す。
    // ＝何も来ていなければ、すぐに負の値が返る。
    switch (ch) {
        case 'i': AT25SF_print_id_status(); break;   // IDとステータスを表示
        case 'e': AT25SF_erase_all();       break;   // 64ページ分を消す
        case 'l': test_log_start();         break;   // 1msタイマーを始める（1024件で止まる）
        case 'd': dump_log_to_pc(end_memo_address);               break;   // CSVで出す
    }
    flash_task();                       // 札の付いたページがあれば書く
    sleep_us(1);
    }
}
