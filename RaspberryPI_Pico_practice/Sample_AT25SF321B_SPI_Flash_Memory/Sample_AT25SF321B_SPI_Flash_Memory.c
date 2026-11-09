#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ========= AT25SF321B(ﾌﾗｯｼｭﾒﾓﾘ)配線表（ピン割り当て表）=====================
// -－ピン割り当て表
// *　１｜!CS(ﾁｯﾌﾟｾﾚｸﾄ) ⇒ GPIO3 ※本番GPI13
// *　２｜SO(IO1)(SerialOutput)(MISO) ⇒ GPIO4 ※本番GPIO12
// *　３｜!WP(IO2)(WriteProtect)書込み保護、内部ﾌﾟﾙｱｯﾌﾟ在り⇒フローティングに
// *　４｜GND
// ‐‐‐―――折り返し
// *　５｜SI(IO0)(SreialInput)(MOSI) ⇒ GPIO4 ※本番GPIO11
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
#define PAGE_PROGRAM 0X02 //0.4ms:1byte~256byte(1ﾍﾟｰｼﾞ)の範囲内で書込み
#define BLOCK_ERASE_4KB 0X20 //0.4ms:4kB最小ﾌﾞﾛｯｸ(ｾｸﾀ)単位ﾃﾞｰﾀ消去(全ぶFFに)
#define BLOCK_ERASE_32KB 0X52 //120ms:32kBのﾊｰﾌﾌﾞﾛｯｸ単位で消去
#define BLOCK_ERASE_64KB 0XD8 //200ms:64kBの標準ﾌﾞﾛｯｸ単位で消去
#define CHIP_ERASE_ALL 0XC7 //10秒:メモリ全体を消去
#define SUSPENDE 0X75 //書込み、消去動作を一時中断、ﾌﾞﾛｯｸ読込を優先
#define RESUME 0X7A //中断解除、書込み、消去再開
//------ステータスレジスタ操作オペコード
#define READ_STATUS_REGISTER_1 0X05 //Busy判定、書込消去後のﾎﾟｰﾘﾝｸﾞ処理
#define WRITE_STATUS_REGISTER_1 0X01
#define READ_STATUS_REGISTER_2 0X35 
#define  WRITE_STATUS_REGISTER_2 0X31
#define READ_STATUS_REGISTER_3 0X15
#define  WRITE_STATUS_REGISTER_3 0X11
//================================================================
#define AT25SF_DEBUG 1

typedef union{
    uint8_t bytes[256]; //256Bytes (000000h〜0000FFh）
}Page_256B_t; //16384個

typedef union {
    uint8_t bytes[4096];
    Page_256B_t pages[16]; //4096Bytes
}Sector_4kB_t; //1024個

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

// -----タイマー割込みの中で呼ばれる関数や、その他関
bool read_status_register_1(void);

// ↓基本的にはSPIを通信のいつもの方でオペコードを送信する書き方で
// 良いと思うが、特によく使うもの、明示的に書いて処理を書いておいたほうが
// 良いものは別に関数としてまとめておくのがよいかな。
// 書き込んだら、書き込んだ分だけ、アドレスを進めるカウンターが欲しいな。
// 書込み終わったらカウンタを進める処理をする。
// アドレスは24bitだな丁度よい型がないな。uint8_tを3個、0x00 00 00
// みたいにして増やしていくのが良いのだろうか？
// 全送信データは書込みなら40bit

uint32_t memory_address = 0x00000000;

void AT25SF_write(uint8_t opecode);

void AT25SF_write(uint8_t opecode)
{
    // if(opecode==NULL) return;

    uint8_t tx_buf[4] = {0};
    tx_buf[1] = (memory_address >> 16) & 0xFF;
    tx_buf[2] = (memory_address >> 8) & 0xFF;
    tx_buf[3] = memory_address;


    gpio_put(AT25SF_PIN_CS, 0);
    spi_write_blocking(AT25SF_SPI_PORT, tx_buf, 5);
    gpio_put(AT25SF_PIN_CS, 1);
}


// ↓初期化関数の中で呼ぶ全メモリ消去関数
void AT25_ERASE_ALL();

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

    while (true) {
        uint8_t id[3] = {0};
        AT25SF_read_jedec_id(id);
        sleep_ms(1000);
    }
}
