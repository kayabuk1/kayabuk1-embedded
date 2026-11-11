#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"

// ========= AT25SF321B(ﾌﾗｯｼｭﾒﾓﾘ)配線表（ピン割り当て表）=====================
// -－ピン割り当て表
// *　１｜!CS(ﾁｯﾌﾟｾﾚｸﾄ) ⇒ GPIO3 ※本番GPI13
// *　２｜SO(IO1)(SerialOutput)(MISO) ⇒ GPIO4 ※本番GPIO12
// *　３｜!WP(IO2)(WriteProtect)書込み保護、内部ﾌﾟﾙｱｯﾌﾟ在り⇒フローティングに
// *　４｜GND
// ‐‐‐―――折り返し
// *　５｜SI(IO0)(SreialInput)(MOSI) ⇒ GPIO7 ※本番GPIO11
// *　６｜SCLK ⇒ GPIO6 ※本番GPIO12
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
#define AT25SF_SPI_BAUDRATE   (1000 * 1000)

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
#define AT25SF_BLOCK_ERASE_4KB 0X20 //0.4ms:4kB最小ﾌﾞﾛｯｸ(ｾｸﾀ)単位ﾃﾞｰﾀ消去(全ぶFFに)
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
// #endif

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
    uint8_t senor_binary;
    uint16_t target_speed;
    uint16_t left_speed;
    uint16_t right_speed;
    int8_t left_motor_duty; //逆回転時にマイナスduty使用するため符号あり
    int8_t right_motor_duty;
    int16_t target_angular_velocity;
    int16_t now_angular_velocity;
    uint16_t vcc_mv;
}log_data_t; //計16byte

volatile log_data_t log_data_buf = {0};

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

// ------ハードウェアドライバ部分
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

// -----タイマー割り込みで書込み＆読み出す部分
bool AT25SF_write_timer_start(int64_t intervar_us);
// void AT25SF_read_timer(uint64_t memory_address, );
// 何を渡すか。

// -----タイマー割込みの中で呼ばれる関数や、その他基本関数

void cs_low(void);
void cs_low(void){
    gpio_put(AT25SF_PIN_CS, 0);
}
void cs_high(void);
void cs_high(void){
    gpio_put(AT25SF_PIN_CS, 1);
}


uint8_t read_status_register_1(void);
/**
 * @brief ステータスレジスタ1を読み出す関数
 * @return 取得したステータスレジスタ1の8bitデータ
 */
uint8_t read_status_register_1(void)
{
    uint8_t data;
    cs_low();
    spi_write_read_blocking(
        AT25SF_SPI_PORT, READ_STATUS_REGISTER_1, &data, 1);
    cs_high();
    return data;
}

bool read_status_is_busy(void);
/**
 * @brief ステータスレジスタ1を読み出す関数
 * @return 取得したステータスレジスタ1のBIT0の 0 or 1
 */
bool read_status_is_busy(void)
{
    bool status;
    uint8_t opecode = READ_STATUS_REGISTER_1;
    cs_low();
    spi_write_read_blocking(
        AT25SF_SPI_PORT, &opecode, &status, 1);
    cs_high();
    return status & 0x01;
    // Ready/BusyBITが 1の時Busy,0の時Ready。
}

static repeating_timer_t polling_timer;
// ↑add_repeating_timer_us() に渡すタイマー管理用の構造体は、
// タイマーが動いている間ずっとメモリ上に生き残っている必要がある。
bool start_polling(uint64_t interval_us, bool(*func)(void));
bool start_polling(uint64_t interval_us, bool(*func)(void))
{
    return add_repeating_timer_us(
        interval_us, func, NULL, &polling_timer
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
        cs_low();
        spi_write_blocking(AT25SF_SPI_PORT, &opecode, 1);
        cs_high();
    }
}

static uint32_t memory_address = 0x00000000; //00000000h~3FFFFFh
static uint8_t page_count = 0;

void AT25SF_write_byte(uint8_t opecode, uint8_t data);
void AT25SF_write_byte(uint8_t opecode, uint8_t data)
{
    if(opecode==NULL) return;
    if(read_status_is_busy()){
        return;
    }else{
        uint8_t tx_buf[5] = {0};
        tx_buf[0] = opecode;
        tx_buf[1] = (memory_address >> 16) & 0xFF;
        tx_buf[2] = (memory_address >> 8) & 0xFF;
        tx_buf[3] = memory_address;
        tx_buf[4] = data;
        cs_low();
        spi_write_blocking(AT25SF_SPI_PORT, tx_buf, 5);
        cs_high();
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
void AT25SF_write_page(uint8_t opecode, uint8_t data_array[]);
void AT25SF_write_page(uint8_t opecode, uint8_t data_array[])
{

}
// log_dataを送る前に、データを取得して log_data_bufに保存する処理が必要か。
void AT25SF_write_log();
void AT25SF_write_log() //log_dataを送るのは明らかなので引数なし
{
    uint8_t opecode = AT25SF_PAGE_PROGRAM;
    if(read_status_is_busy()){
        return;
    }else{
        size_t max = sizeof(log_data_t)/sizeof(uint8_t);
        uint8_t tx_buf[max + 4];
        tx_buf[0] = opecode;
        tx_buf[1] = (memory_address >> 16) & 0xFF;
        tx_buf[2] = (memory_address >> 8) & 0xFF;
        tx_buf[3] = memory_address;
        uint32_t ints = save_and_disable_interrupts();
        tx_buf[4] = log_data_buf.sensor_displacement;
        tx_buf[5] = log_data_buf.senor_binary;
        tx_buf[6] = (log_data_buf.target_speed >> 8) & 0xFF;
        tx_buf[7] = log_data_buf.target_speed & 0xFF;
        tx_buf[8] = (log_data_buf.left_speed >> 8) & 0xFF;
        tx_buf[9] = log_data_buf.left_speed & 0xFF;
        tx_buf[10] = (log_data_buf.right_speed >> 8) & 0xFF;
        tx_buf[11] = log_data_buf.right_speed & 0xFF;
        tx_buf[12] = log_data_buf.left_motor_duty;
        tx_buf[13] = log_data_buf.right_motor_duty;
        tx_buf[14] = (uint16_t)(log_data_buf.target_angular_velocity >> 8) & 0xFF;
        tx_buf[15] = log_data_buf.target_angular_velocity & 0xFF;
        tx_buf[16] = (uint16_t)(log_data_buf.now_angular_velocity >> 8) & 0xFF;
        tx_buf[17] = log_data_buf.now_angular_velocity & 0xFF;
        tx_buf[18] = (log_data_buf.vcc_mv >> 8) & 0xFF;
        tx_buf[19] = log_data_buf.vcc_mv & 0xFF;
        restore_interrupts(ints);
        cs_low();
        spi_write_blocking(AT25SF_SPI_PORT, tx_buf, max+4);
        cs_high();
        // 1MHzで20バイト送ると約160µsかかる（8μs*20）。
        // 1msの制御周期のうち、16%をSPIの待ちで使ってしまう。
        // ➡フラッシュへの記録SPI通信で割込みを行うのは難しい。
        memory_address += sizeof(log_data_t); 
        // 10進数の「16」は16進数で 0x10 なので、
        // 毎回下から2桁目が +1 される（0x00 ➔ 0x10 ➔ 0x20 ➔ ...0xF0）。
        // 16回目で桁上がりして、0x100になって下位2ビットが0x00に戻る。
        // これでピッタリ256バイト(1ページ)になる。
        if((memory_address && 0X0F) != 0x00)
        // 0xFF = 0b1111_1111 = 255
        // 10進数の16は 2進数では 0b00010000。
        // 16ずつ増やすと、下位4ビットはずっと 0000 のまま固定。
        // なので、アドレスを0x0Fでマスクして下位4ビットを取り出して、
        // それが 0000 で無ければずれていると判定してアドレスを修正する。
        {
            memory_address = (memory_address & ~0x0F)+0x10;
        }
    }
}


// ↓初期化関数の中で呼ぶ全メモリ消去関数
void AT25SF_erase_all();

void AT25SF_read_jedec_id(uint8_t id[3]) {
    uint8_t opecode = JEDEC_ID_READ;
    gpio_put(AT25SF_PIN_CS, 0);
    spi_write_blocking(AT25SF_SPI_PORT, &opecode, 1);
    spi_read_blocking(AT25SF_SPI_PORT, 0x00, id, 3);
    gpio_put(AT25SF_PIN_CS, 1);
    printf("%02X %02X %02X\n", id[0], id[1], id[2]);
}


int main()
{
    stdio_init_all();
    AT25SF_init();

    start_polling(PAGE_WRITE_TAKES_TIME, read_status_is_busy);

    while (true) {
        uint8_t id[3] = {0};
        AT25SF_read_jedec_id(id);
        sleep_ms(1000);
    }
}
