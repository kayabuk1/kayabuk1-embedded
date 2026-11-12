
// ◆2026年11月12日考えメモとりあえずバフに書き込む処理はある程度書けた
// 次は、256byte 一気にAT25SFに書き込む処理だな。
// ◆他やっておきたいこと
// ●static inline修飾子を付けて、内部でのみ使う関数は、名前の競合の
// 心配がないので、より短く簡潔な名称で使いやすく。例AT25SF_cs_low⇒cs_low
// ●全消去終了完了を知らせるブザーを鳴らす回路をブレッドボードに組む。
// ●関数の並びがグチャグチャなので、整理する。
// ●ページ単位での処理が可能になったら、セクタ4k単位での書込み処理を実装する
// ●ページ単位の書き込み処理が実行可能になったら、オシロで掛かる時間を実測する。
// 

#include <stdio.h>
#include <stdint.h>
#include<string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "buzzer/buzzer.h"
#include "pico_led_sw.h"

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
#define TX_SIZE_WRITE_PAGE 260

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
}sensors_data_t; //計16byte



#if AT25SF_DEBUG==1
// ダミーデータを設定。
volatile sensors_data_t now_sensors_data 
= {
    .sensor_displacement = 0x0F,
    .sensor_binary = 0xF0,
    .target_speed = 0xF0F0,
    .left_speed = 0x00FF,
    .right_speed = 0xFF00,
    .left_motor_duty = 0x0F,
    .right_motor_duty = -16, //int8_t なので 0xFF=256は範囲外。負の値でシフト動作確認。
    .target_angular_velocity = -256,　//int16_tなので0xFF00では範囲外
    .now_angular_velocity = 0x00FF,
    .vcc_mv = 0X0FF0
};
#else
volatile sensors_data_t now_sensors_data = {0};
#endif

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
// void AT25SF_read_timer(uint64_t memory_address, );
// 何を渡すか。

// -----タイマー割込みの中で呼ばれる関数や、その他基本関数

static inline void AT25SF_cs_low(void);
static inline void AT25SF_cs_low(void){
    gpio_put(AT25SF_PIN_CS, 0);
}
static inline void AT25SF_cs_high(void);
static inline void AT25SF_cs_high(void){
    gpio_put(AT25SF_PIN_CS, 1);
}

static inline void AT25SF_write_enable(void);
static inline void AT25SF_write_enable(void){
    // WEL: WriteEnableLatch
    uint8_t tx = AT25SF_WRITE_ENABLE;
    AT25SF_cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, &tx, 1);
    AT25SF_cs_high();
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
    uint8_t tx[2] = {READ_STATUS_REGISTER_1, 0x00};
    uint8_t rx[2] = {0};
    AT25SF_cs_low();
    spi_write_read_blocking(
        AT25SF_SPI_PORT, tx, rx, 2);
    AT25SF_cs_high();
    return rx[1];
}

bool read_status_is_busy(void);
/**
 * @brief ステータスレジスタ1を読み出す関数
 * @return 取得したステータスレジスタ1のBIT0の 0 or 1
 */
bool read_status_is_busy(void)
{
    uint8_t opecode = READ_STATUS_REGISTER_1;
    uint8_t tx[2] = {opecode, 0x00};
    uint8_t rx[2] = {0};
    AT25SF_cs_low();
    spi_write_read_blocking(
        AT25SF_SPI_PORT, tx, rx, 2);
    // opecodeの8バイト後のダミービット8バイト送信して、
    // 8ビットのレジスタ1のデータが返ってくるので、送るデータ数は2。
    // SPIはリングバッファなので、受信するには同じサイズのデータを送っている。
    AT25SF_cs_high();
    bool status = rx[1] & 0x01;
    // なので opecode 送信時にもダミーデータ8bitを受信している。
    // なので、ステータスレジスタが入っているのはrx[1]。
    return status;
    // Ready/BusyBITが 1の時Busy,0の時Ready。
}
// ◆割り込み処理考えメモ
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
//     start_polling(PAGE_WRITE_TAKES_TIME, read_status_is_busy);
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

void AT25SF_send_opecode(uint8_t opecode);
void AT25SF_send_opecode(uint8_t opecode)
{
    if(read_status_is_busy())
    {
        return;
    }
    else
    {   
        AT25SF_cs_low();    
        spi_write_blocking(AT25SF_SPI_PORT, &opecode, 1);
        AT25SF_cs_high();
    }
}

static uint32_t memory_address = 0x00000000; //00000000h~3FFFFFh
static uint8_t page_count = 0;

void AT25SF_write_byte(uint8_t opecode, uint8_t data);
void AT25SF_write_byte(uint8_t opecode, uint8_t data)
{
    // if(opecode==NULL) return; //ninja: build stopped: subcommand failed.
    if(read_status_is_busy()){
        return;
    }else{
        uint8_t tx_buf[5] = {0};
        tx_buf[0] = opecode;
        tx_buf[1] = (memory_address >> 16) & 0xFF;
        tx_buf[2] = (memory_address >> 8) & 0xFF;
        tx_buf[3] = memory_address;
        tx_buf[4] = data;
        AT25SF_write_enable();
        AT25SF_cs_low();
        spi_write_blocking(AT25SF_SPI_PORT, tx_buf, 5);
        AT25SF_cs_high();
        // 256byteを越えてページが跨ってしまう場合は、ページを変える
        // 処理を書く必要がある。
        /*◆memory_addressについて
        ●ページ番号は memory_address >> 8
        ●ページ内の位置は memory_address & 0xFF
        ●セクタ番号は memory_address >> 12 で計算できるのはなぜか。
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
}

// ◆logデータをそのまま16byte取得ごとにAT25SFへSPI通信するのは、
// オーバヘッドが大きく、マイクロマウス制御に影響を及ぼすため、
// 間にダブルバッファを挟み256byte一気に送信する方針に変更。
// そのためコメントアウト。後ほど削除するか検討。
// // log_dataを送る前に、データを取得して now_sensors_dataに保存する処理が必要か。
// void AT25SF_write_log(void);
// void AT25SF_write_log(void) //log_dataを送るのは明らかなので引数なし
// {
//     uint8_t opecode = AT25SF_PAGE_PROGRAM;
//     if(read_status_is_busy()){
//         return;
//     }else{
//         size_t max = sizeof(sensors_data_t)/sizeof(uint8_t);
//         uint8_t tx_buf[max + 4];
//         tx_buf[0] = opecode;
//         tx_buf[1] = (memory_address >> 16) & 0xFF;
//         tx_buf[2] = (memory_address >> 8) & 0xFF;
//         tx_buf[3] = memory_address;
//         uint32_t ints = save_and_disable_interrupts();
//         tx_buf[4] = now_sensors_data.sensor_displacement;
//         tx_buf[5] = now_sensors_data.sensor_binary;
//         tx_buf[6] = (now_sensors_data.target_speed >> 8) & 0xFF;
//         tx_buf[7] = now_sensors_data.target_speed & 0xFF;
//         tx_buf[8] = (now_sensors_data.left_speed >> 8) & 0xFF;
//         tx_buf[9] = now_sensors_data.left_speed & 0xFF;
//         tx_buf[10] = (now_sensors_data.right_speed >> 8) & 0xFF;
//         tx_buf[11] = now_sensors_data.right_speed & 0xFF;
//         tx_buf[12] = now_sensors_data.left_motor_duty;
//         tx_buf[13] = now_sensors_data.right_motor_duty;
//         tx_buf[14] = ((uint16_t)now_sensors_data.target_angular_velocity >> 8) & 0xFF;
//         tx_buf[15] = now_sensors_data.target_angular_velocity & 0xFF;
//         tx_buf[16] = ((uint16_t)now_sensors_data.now_angular_velocity >> 8) & 0xFF;
//         tx_buf[17] = now_sensors_data.now_angular_velocity & 0xFF;
//         tx_buf[18] = (now_sensors_data.vcc_mv >> 8) & 0xFF;
//         tx_buf[19] = now_sensors_data.vcc_mv & 0xFF;
//         restore_interrupts(ints);
//         AT25SF_write_enable();
//         AT25SF_cs_low();
//         spi_write_blocking(AT25SF_SPI_PORT, tx_buf, max+4);
//         AT25SF_cs_high();
//         // 1MHzで20バイト送ると約160µsかかる（8μs*20）。
//         // 1msの制御周期のうち、16%をSPIの待ちで使ってしまう。
//         // ➡フラッシュへの記録SPI通信で割込みを行うのは難しい。
//         memory_address += sizeof(sensors_data_t); 
//         // 10進数の「16」は16進数で 0x10 なので、
//         // 毎回下から2桁目が +1 される（0x00 ➔ 0x10 ➔ 0x20 ➔ ...0xF0）。
//         // 16回目で桁上がりして、0x100になって下位2ビットが0x00に戻る。
//         // これでピッタリ256バイト(1ページ)になる。
//         // if((memory_address & 0X0F) != 0x00)
//         // // 0xFF = 0b1111_1111 = 255
//         // // 10進数の16は 2進数では 0b00010000。
//         // // 16ずつ増やすと、下位4ビットはずっと 0000 のまま固定。
//         // // なので、アドレスを0x0Fでマスクして下位4ビットを取り出して、
//         // // それが 0000 で無ければずれていると判定してアドレスを修正する。
//         // {
//         //     memory_address = (memory_address & ~0x0F)+0x10;
//         // }⇒ここのifは、16刻みで送るなら、
//         // なくても大丈夫と判断。一時コメントアウト。
//     }
// }

static uint now_fill = 0; //割り込みが今詰めているバッファの番号（0か1）
static int ready_buf = -1; //書き込みを待っているバッファの番号。無ければ -1
static uint pack_dropped = 0; //  // 書込みあふれ。数えておき、このページは捨てる
static uint32_t total_bytes = 0; //バッファの中で、次に詰める位置（0〜255）
static uint16_t n_bytes = 0;
static uint8_t n_pages = 0;
void pack_log_into_buf(void);
void pack_log_into_buf(void)
{   
    n_pages = (total_bytes / 256) % 2;     //16/256=0%2=0,256/256=1%2=1,272/256=1%2=1,512/256=2%2=0
    n_bytes = total_bytes % 256; //16%256=16,32%256=32...256%256=0
    // if (n_bytes >= 256){
    //     n_pages = n_pages ^ 1;
    // } ifを使った方が分かりやすいかもしれない、、、。
    log_page_buf[n_pages].bytes[n_bytes] = now_sensors_data.sensor_displacement;
    log_page_buf[n_pages].bytes[n_bytes+1] = now_sensors_data.sensor_binary;
    log_page_buf[n_pages].bytes[n_bytes+2] = (now_sensors_data.target_speed >> 8) & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+3] = now_sensors_data.target_speed & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+4] = (now_sensors_data.left_speed >> 8) & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+5] = now_sensors_data.left_speed & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+6] = (now_sensors_data.right_speed >> 8) & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+7] = now_sensors_data.right_speed & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+8] = now_sensors_data.left_motor_duty;
    log_page_buf[n_pages].bytes[n_bytes+9] = now_sensors_data.right_motor_duty;
    log_page_buf[n_pages].bytes[n_bytes+10] = ((uint16_t)now_sensors_data.target_angular_velocity >> 8) & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+11] = now_sensors_data.target_angular_velocity & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+12] = ((uint16_t)now_sensors_data.now_angular_velocity >> 8) & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+13] = now_sensors_data.now_angular_velocity & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+14] = (now_sensors_data.vcc_mv >> 8) & 0xFF;
    log_page_buf[n_pages].bytes[n_bytes+15] = now_sensors_data.vcc_mv & 0xFF;
    // ◆いや、forで回すのでなくて、割り込みで呼ばれることを想定して、
    // 配列の指定を、%16,%256などを使って、staticかグローバルにして管理して、
    // 値の格納処理後に、カウントを進める書き方に変える必要があるかな。
    // それで256byte=1pageバッファに値が溜まったら、startwritelogAT25SF
    // みたいな非同期処理の関数を呼び出してスタートさせる処置みたいに掛けたら
    // 理想だな。
    total_bytes += 16;

    // if(n_bytes >= 256) start_write_AT25SF();
    // みたいに256になったら書込み処理をスタートさせたい。
    if(total_bytes % 256 == 0){ //一ページ溜まった瞬間。
        if(ready){
            total_bytes -= 256; //今のページを捨てて同じページに最初から
        }
    }
}

// 256byte=1pageをまとめて書き込む関数。
void AT25SF_write_1page(void);
void AT25SF_write_1page(void)
{
    if(read_status_is_busy()){
        return;
    }else{
        // uint16_t max = (sizeof(Page_256B_t)/2) + 4;
        // uint8_t tx[max] = {0};
        // ⚠↑の書き方は間違い。配列の要素数指定する max が、
        // コンパイル時に決まらず、実行時に決まる普通の変数(可変長配列)
        // になっているためサイズがコンパイル時に分からないため、
        // ={0};で一括に初期化できない。
        uint8_t tx[TX_SIZE_WRITE_PAGE] = {0};
        uint8_t opecode = AT25SF_PAGE_PROGRAM;
        tx[0] = opecode;
        tx[1] = (memory_address >> 16) & 0xFF;
        tx[2] = (memory_address >> 8) & 0xFF;
        tx[3] = memory_address;
        uint32_t ints = save_and_disable_interrupts();
        memcpy(
            &tx[4], log_page_buf[!n_pages].bytes, 256);
        restore_interrupts(ints);
        // void *memcpy
        // (void *__restrict__, const void *__restrict__, size_t)
        // コピー先のメモリ領域のポインタ, コピー元のメモリ領域のポインタ, コピーするサイズ（バイト数）
        // memcpy（メモリ・コピー）
        // あるメモリ領域の内容を別のメモリ領域へ高速にコピーするための
        // 標準ライブラリ関数。
        AT25SF_write_enable();
        AT25SF_cs_low();
        spi_write_blocking(AT25SF_SPI_PORT, tx, TX_SIZE_WRITE_PAGE);
        AT25SF_cs_high();
        memory_address += 256; 
    }
}


// ↓初期化関数の中で呼ぶ全メモリ消去関数
// ➡毎回電源起動時の初期化で呼んでしまうと、ログのデータを読み出そうと電源
// を入れた時にログが消えてしまう。
// ➡SW２を押した時に実行する様に変更。
void AT25SF_erase_all(void);
void AT25SF_erase_all(void){
 if(get_tactSW2())
 {
    uint8_t tx = AT25SF_CHIP_ERASE_ALL;
    AT25SF_write_enable();
    AT25SF_cs_low();
    spi_write_blocking(AT25SF_SPI_PORT, &tx, 1);
    AT25SF_cs_high();
    // CSピンを High に戻した瞬間にIC内部で自動消去が始まる。
    // Chip Erase(全ﾒﾓﾘ領域の初期化※全て0xFF=１になる)
    // 全領域の消去には約10秒程度掛かる見込み
    while (read_status_is_busy())
    {
        sleep_ms(1000);
    }
    for(int i=0; i <1000; i++)
    {
        beepProcess();
        out_led2(1);
        sleep_ms(1);
    }
 }
 else
 {
  return;
 }
}


void AT25SF_read_jedec_id(uint8_t id[3]) {
    uint8_t opecode = JEDEC_ID_READ;
    gpio_put(AT25SF_PIN_CS, 0);
    spi_write_blocking(AT25SF_SPI_PORT, &opecode, 1);
    spi_read_blocking(AT25SF_SPI_PORT, 0x00, id, 3);
    gpio_put(AT25SF_PIN_CS, 1);
    printf("%02X %02X %02X\n", id[0], id[1], id[2]);
}

void AT25SF_init(void); 
//この名前だと最初の初期化処理もするみたいに見えるな

void AT25SF_init(void) {
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
}
void AT25_hardware_setup(vodi)
{
  gpio_set_function(10, GPIO_FUNC_SIO);
  gpio_set_function(13, GPIO_FUNC_SIO);
  gpio_set_dir(10, GPIO_OUT);
 // Picoボード上のSW0の入力を受け付けるための準備
  gpio_set_dir(13, GPIO_IN);
  // SWに接続されているGPIOのpull up抵抗をONに。
  gpio_pull_up(13);
}
/*led2を点滅*/
void out_led2(bool on)
{
    gpio_put(10, on);
}
// SWの状態を読取る関数
bool get_tactSW2(void)
{
    return !gpio_get(13);
    // ボード上のSWは押すとLow、通常時Highなので、
    // 直感的に押されたとき「１」になるように、
    // 「!」で論理反転をする。
}

int main()
{
    stdio_init_all();
    AT25_hardware_setup();
    initBeep();
    AT25SF_init();

    while (true) {
        AT25SF_erase_all();
        uint8_t id[3] = {0};
        AT25SF_read_jedec_id(id);
        sleep_ms(1000);
    }
}
