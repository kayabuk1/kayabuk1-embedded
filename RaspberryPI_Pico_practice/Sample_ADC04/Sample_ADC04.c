#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"

#define PIN_MOSI 7 //MCP3002 物理ピン5番D_IN
#define PIN_MISO 4 //MCP3002 物理ピン6番D_OUT
#define PIN_CS 3 //MCP3002 物理ピン1番!CS/SHDN(ﾁｯﾌﾟｾﾚｸﾄ/ｼｬｯﾄﾀﾞｳﾝ入力)
// ↑※通信相手指定アクティブロー
#define PIN_SCK 6 //MCP3002 物理ピン7番CLK
#define SPI_PORT spi0 //picoに2つあるSPI通信ｺﾝﾄﾛｰﾗの1つ目を使うよ、と指定。



typedef struct ADC_DATA_t
{
    /* data */
    uint16_t max_value;
    uint16_t min_value;
    uint16_t now_value;
} ADC_DATA_t;

volatile ADC_DATA_t adc_data = {0, 0xFFFF, 0}; 
// ADCデータ構造体の初期化
// min_valueはどんな値を測っても「０」より小さくならないので、
// 初期値はありえる最大の値にしておくか、最初に測った値を入れてから比べ始める。

/**
 * @brief ハードウェア初期化処理
 */
void init_hardware(void){
    stdio_init_all();

    spi_init(SPI_PORT, 1000 * 1000); 
    //SPI通信の初期化。通信速度は1MHzに設定。
// ●ﾃﾞｰﾀｼｰﾄ記載
// Timing Parameters:
// Clock Frequency fCLK 
// 3.2MHZ VDD = 5V (Note 2)
// 1.2MHz VDD = 2.7V (Note 2) より 1MHzで問題なし。
// 8bit/1000*1000※1MHz = 0.000_008秒=8us
    spi_set_format (
    SPI_PORT,       // spi_inst_t SPIインスタンス (spi0 または spi1)
    8,        // uint 1回あたりのデータビット数 (通常は 4 〜 16)
    SPI_CPOL_0,       // spi_cpol_t クロック極性 (SPI_CPOL_0 または SPI_CPOL_1)
    SPI_CPHA_0,       // spi_cpha_t クロック位相 (SPI_CPHA_0 または SPI_CPHA_1)
    SPI_MSB_FIRST 
    // __unused spi_order_t ビットオーダー (SPI_MSB_FIRST または SPI_LSB_FIRST)
    // ※RP2040はMSBファーストにしか対応していない。
    );

    // SPI通信用ピン機能設定
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI); 
    gpio_set_function(PIN_CS,  GPIO_FUNC_SIO);
    
    // ピン方向を出力に設定
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    // ローアクティブの為、CSピンをHIGHにしておく。通信開始時にLOWにする。
}

static inline uint16_t hw_mcp3002_read(uint8_t channel)
{   
    if (channel > 1) {
        channel = 0; //CH0かCH1しかないので、範囲外の値が来たらCH0にする。
    }
    uint8_t tx_buf[2] = {0};
    uint8_t rx_buf[2] = {0};

// Start(1), SGL/DIFF(1), ODD/SIGN(channel), MSBF(1) -> 
// 0b1101_0000 (0xD0) または 0b1111_0000 (0xF0)
    // 0, Start, SGL, ODD, MSBF, 0, 0, 0
    tx_buf[0] = 0b01101000 | ((channel & 0x01) << 4); 
     // ※ tx_buf[1], tx_buf[2] は 0x00 のまま（Don't Careのダミーバイトとして送信）
// ●MCP3002の!CSへチャンネル選択Low信号入力後の制御ビットの構造
// ・Start bit（スタートビット）: 最初に入力される High（1）のビット。
// これを受信するとICは通信開始と認識する。
// ・SGL/DIFF: シングルエンドモード（1）か、疑似差動モード（0）かを選択。
// ・ODD/SIGN: チャンネルの選択および極性を指定。
// 今回はVss(GND)を基準に電圧を比較するシングルエンドモードで、CH0を選択するため 0 に設定。
// ・MSBF: 出力データフォーマットを指定。1 に設定すると変換結果を MSBファースト
// （最上位ビットから順）で受信。


    gpio_put(PIN_CS, 0); // CSをLOWにして通信開始
    spi_write_read_blocking(SPI_PORT, tx_buf, rx_buf, 2);
    gpio_put(PIN_CS, 1); // CSをHIGHにして通信終了
// Raspberry Pi Pico (RP2040) の Pico SDK における 
// spi_write_read_blocking() は、SPIデバイスに対してデータの送信と受信を同時に行う
// ブロッキング（同期）関数。
// 関数仕様
// • 関数宣言: int spi_write_read_blocking 
// (spi_inst_t *spi, const uint8_t *src, uint8_t *dst, size_t len)
// • 引数:
// 	• spi: 使用するSPIインスタンス (spi0 または spi1)
// 	• src: 送信データ（バッファ）へのポインタ
// 	• dst: 受信データを格納するバッファへのポインタ
// 	• len: 送受信するバイト数

    uint16_t raw_value = (rx_buf[0] &0x03) <<8 | rx_buf[1];
    return raw_value;
    // 10ビットのデータを取得。
    // 0b0000_0011 (0x03) でマスクして、rx_buf[1] の下位2ビットを取得し、
    // 左に8ビットシフトして0b0000_0011_0000_0000の形にする。
    // その後、rx_buf[2] の値とビット単位でOR演算を行い、10ビットのADC値を取得する。
    
    //※ 受信データの処理 ⇒　デジタルコードから電圧への変換の処理は重いため
    // データ取得では行わず、構造体にはデジタルコードのまま格納して、
    // 読みだすときに変換処理をする方が効率的。

}

bool adc_timer_callback(repeating_timer_t *rt)
{   
    // volatile ADC_DATA_t *p_adc_data = (ADC_DATA_t *)rt->user_data;
    // // adc_dataはvolatile が付いているが、
    // // コールバックでは(ADC_DATA_t *)にキャストしている為、
    // // そこで volatile が失われる。
    // hw_mcp3002_read(0, &p_adc_data->now_value);

    adc_data.now_value = hw_mcp3002_read(0); // CH0のADC値を取得して代入。
    return true; // タイマーを継続する
}

static repeating_timer_t adc_timer;

int main()
{
    init_hardware();

    add_repeating_timer_us
    (-1000000, // 1秒ごとにタイマー割り込み
    adc_timer_callback, NULL, &adc_timer);

    while (true) {
    
    // 1. 割り込みの状態を保存して無効化
    uint32_t ints = save_and_disable_interrupts();
    uint16_t raw_value = adc_data.now_value; //コピーだけ
    // 2. 割り込み状態を元に戻す
    restore_interrupts(ints);
    // save_and_disable_interrupts()で割込みを一瞬止めて、
    // 構造体をローカル変数にコピーしてから表示すると、表示の途中で値が書き換わらない。

    // MCP3002から得られた10ビットのデジタル出力コード（0～1023）を、
    // 実際の入力アナログ電圧V_in(V)に直す。
    // 1LSB(1bit辺りの電圧変化量)=V_ref / 2^10 = 3.3V / 1024 = 0.00322265625V 
    // ≒ 3.22mV
    // V_in = (raw_value / 1024.0) * 3.3V
    uint16_t voltage_value = (uint16_t)(((uint32_t)raw_value * 3300) / 1024);
    // mV単位で計算
    // PicoのRP2040には浮動小数点を専用に計算する回路（FPU）がないので、
    // 小数の計算はソフトで行われて少し重くなる。
    // 先に掛けてから割るのがコツ。1023 × 3300 = 3,375,900で、
    // 16ビット（最大65,535）を超えるので、uint32_tに広げてから掛ける。

    printf("ADC Value: %u mV\n", voltage_value);
    sleep_ms(1000);
    }
}

