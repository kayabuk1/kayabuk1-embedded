#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "adc_mcp3002/adc_mcp3002.h"

#define MCP3002_PIN_MOSI 7 //MCP3002 物理ピン5番D_IN 
#define MCP3002_PIN_MISO 4 //MCP3002 物理ピン6番D_OUT
#define MCP3002_PIN_CS 3 //MCP3002 物理ピン1番!CS/SHDN(ﾁｯﾌﾟｾﾚｸﾄ/ｼｬｯﾄﾀﾞｳﾝ入力)
// ↑※通信相手指定アクティブロー
#define MCP3002_PIN_SCK 6 //MCP3002 物理ピン7番CLK
#define MCP3002_SPI_PORT spi0 //picoに2つあるSPI通信ｺﾝﾄﾛｰﾗの1つ目を使うよ、と指定。
#define MCP3002_VREF_MV 3300 //⚠PicoのGPIOは3.3Vまでなので、基準電圧は3.3Vとする。
#define MCP3002_RESOLUTION 1024 // 10bit ADCなので、2^10=1024
#define MCP3002_BAUDRATE   (1000 * 1000)
#define MCP3002_DEBUG 0 // デバッグ用のprintf()を有効にするかどうかのフラグ

// ========= MCP3002配線表（ピン割り当て表） =====================
// -－ピン割り当て表
// *　１｜!CS/SHDN(ﾁｯﾌﾟｾﾚｸﾄ/ｼｬｯﾄﾀﾞｳﾝ入力) ⇒ GPIO3
// *　２｜CH0 ⇒ 可変抵抗1の2番端子(真ん中)※電圧を測定したいものを接続
// *　３｜CH1 ⇒ 可変抵抗2の2番端子(真ん中)※電圧を測定したいものを接続
// *　４｜VSS(GND)
// ‐‐‐―――折り返し
// *　５｜D_IN(MOSI) ⇒ GPIO7
// *　６｜D_OUT(MISO) ⇒ GPIO4
// *　７｜SCLK(CLK) ⇒ GPIO6
// *　８｜VDD/VREF(3.3V)※入力電圧がそのままAD変換の基準電圧として使用される。

typedef struct ADC_DATA_t
{
    /* data */
    uint16_t max_value;
    uint16_t min_value;
    uint16_t now_value;
} ADC_DATA_t;

static volatile ADC_DATA_t adc_data[2] 
= {{0, MCP3002_RESOLUTION-1, 0},{0, MCP3002_RESOLUTION-1, 0}}; 
// ADCデータ構造体の初期化 {0,1023,0} → {0, MCP3002_RESOLUTION-1, 0} に変更。
// min_valueはどんな値を測っても「０」より小さくならないので、
// 初期値はありえる最大の値にしておくか、最初に測った値を入れてから比べ始める。
// ◆volatile 
// は、コンパイラに対して「この変数の値は、ソースコードに書かれていない外部の要因で、
// 予期せず変更されることがある」と伝えるための型修飾子
// これにより、volatile を付けた変数は、レジスタへのキャッシュや保持をせず、
// アクセスするたびに毎回実際のメモリ（またはハードウェアアドレス）から値を読み書きする。
// ※コンパイラの最適化を制限する。
// ⇒変数への読み書きが「無駄である（変化していない）」とコンパイラが判断し、
// メモリから読み直す処理や、書き込み処理を勝手に省いてしまうのを防ぐ。

/**
 * @brief raw値をmVに変換する関数
 * @param raw 10ビットのデジタル出力コード（0～1023）
 * @return uint16_t mV単位の電圧値
 */
static inline uint16_t raw_to_mV(uint16_t raw) 
{
    return (uint16_t)(((uint32_t)raw * MCP3002_VREF_MV) / MCP3002_RESOLUTION);    // mV単位で計算
    // MCP3002から得られた10ビットのデジタル出力コード（0～1023）を、
    // 実際の入力アナログ電圧V_in(V)に直す。
    // 1LSB(1bit辺りの電圧変化量)=V_ref / 2^10 = 3.3V / 1024 = 0.00322265625V 
    // ≒ 3.22mV
    // V_in = (raw_value / 1024.0) * 3.3V

    // PicoのRP2040には浮動小数点を専用に計算する回路（FPU）がないので、
    // 小数の計算はソフトで行われて少し重くなる。
    // 先に掛けてから割るのがコツ。1023 × 3300 = 3,375,900で、
    // 16ビット（最大65,535）を超えるので、uint32_tに広げてから掛ける。
}

#if 0
/**
 * @brief ハードウェア初期化処理
 */
void mcp3002_init(void)
{
    stdio_init_all();

    spi_init(MCP3002_SPI_PORT, MCP3002_BAUDRATE); 
    //SPI通信の初期化。通信速度は1MHzに設定。
    // ●ﾃﾞｰﾀｼｰﾄ記載
    // Timing Parameters:
    // Clock Frequency fCLK 
    // 3.2MHZ VDD = 5V (Note 2)
    // 1.2MHz VDD = 2.7V (Note 2) より 1MHzで問題なし。
    // 8bit/1000*1000※1MHz = 0.000_008秒=8us
    spi_set_format (
        MCP3002_SPI_PORT,       // spi_inst_t SPIインスタンス (spi0 または spi1)
        8,        // uint 1回あたりのデータビット数 (通常は 4 〜 16)
        SPI_CPOL_0,       // spi_cpol_t クロック極性 (SPI_CPOL_0 または SPI_CPOL_1)
        SPI_CPHA_0,       // spi_cpha_t クロック位相 (SPI_CPHA_0 または SPI_CPHA_1)
        SPI_MSB_FIRST 
    // __unused spi_order_t ビットオーダー (SPI_MSB_FIRST または SPI_LSB_FIRST)
    // ※RP2040はMSBファーストにしか対応していない。
    );

    // SPI通信用ピン機能設定
    gpio_set_function(MCP3002_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MCP3002_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(MCP3002_PIN_SCK,  GPIO_FUNC_SPI); 
    gpio_set_function(MCP3002_PIN_CS,  GPIO_FUNC_SIO);
    
    // ピン方向を出力に設定
    gpio_set_dir(MCP3002_PIN_CS, GPIO_OUT);
    gpio_put(MCP3002_PIN_CS, 1);
    // ローアクティブの為、CSピンをHIGHにしておく。通信開始時にLOWにする。
}
#endif

/**
 * @brief MCP3002からADC値を読み取る関数
 * @param channel チャンネル番号 (0 or 1)
 * @return uint16_t ADC値
 */
uint16_t hw_mcp3002_read(uint8_t channel)
{   
    if (channel > 1) {
        channel = 0; //CH0かCH1しかないので、範囲外の値が来たらCH0にする。
        #if DEBUG
        printf("hw_mcp3002_read: channel out of range, set to 0\n");
        #endif
    }
    uint8_t tx_buf[2] = {0};
    uint8_t rx_buf[2] = {0};

    // Start(1), SGL/DIFF(1), ODD/SIGN(channel), MSBF(1) -> 
    // 0b0110_1000 または 0b0111_1000 
    // 0, Start, SGL, ODD, MSBF, 0, 0, 0
    tx_buf[0] = 0b01101000 | ((channel & 0x01) << 4); 
    // ※ tx_buf[1] は 0x00 のまま（Don't Careのダミーバイトとして送信）
    // ●MCP3002の!CSへチャンネル選択Low信号入力後の制御ビットの構造
    // ・Start bit（スタートビット）: 最初に入力される High（1）のビット。
    // これを受信するとICは通信開始と認識する。
    // ・SGL/DIFF: シングルエンドモード（1）か、疑似差動モード（0）かを選択。
    // ・ODD/SIGN: チャンネルの選択および極性を指定。
    // 今回はVss(GND)を基準に電圧を比較するシングルエンドモードで、
    // CH0を選択するため 0 に設定。
    // ・MSBF: 出力データフォーマットを指定。1 に設定すると変換結果を MSBファースト
    // （最上位ビットから順）で受信。

    gpio_put(MCP3002_PIN_CS, 0); // CSをLOWにして通信開始
    spi_write_read_blocking(MCP3002_SPI_PORT, tx_buf, rx_buf, 2);
    gpio_put(MCP3002_PIN_CS, 1); // CSをHIGHにして通信終了
    // Raspberry Pi Pico (RP2040) の Pico SDK における 
    // spi_write_read_blocking() は、SPIデバイスに対してデータの送信と受信
    // を同時に行う。
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
    // 0b0000_0011 (0x03) でマスクして、rx_buf[0] の下位2ビットを取得し、
    // 左に8ビットシフトして0b0000_0011_0000_0000の形にする。
    // その後、rx_buf[1] の値とビット単位でOR演算を行い、10ビットのADC値を取得する。
    
    //※ 受信データの処理 ⇒　デジタルコードから電圧への変換の処理は重いため
    // データ取得では行わず、構造体にはデジタルコードのまま格納して、
    // 読みだすときに変換処理をする方が効率的。
}

/**
 * @brief　ADC値を定期的に読み取るタイマーコールバック関数
 * @param rt タイマー構造体へのポインタ（未使用）
 */
bool adc_timer_callback(__unused repeating_timer_t *rt)
{   
    for (uint8_t ch = 0; ch < 2; ch++) {
    // ●一回のコールバックで、CH0とCH1を続けて読む。
    // 一回の読み取りは1MHzで16クロック、約16µsなので、二回続けても約32µsで終わる。
    // 二つのチャンネルがほぼ同じ時刻の値になるので、扱いやすい。

        uint16_t raw = hw_mcp3002_read(ch); // 今のCHのADC値を取得して代入。
        adc_data[ch].now_value = raw; // 取得したADC値を構造体に格納。
        if (raw > adc_data[ch].max_value) adc_data[ch].max_value = raw;
        if (raw < adc_data[ch].min_value) adc_data[ch].min_value = raw;
        // 最大・最小はコールバックの中で更新する
        // （タイマーを速くしても山や谷を取りこぼさないため）
    }
    return true; // タイマーを継続する

    //●コールバックのたびにチャンネルを交互に切り替える案も使えるが、今回は使わない。
    // staticで前回のチャンネルを覚えて、毎回反転させる形。
    // bool adc_timer_callback(repeating_timer_t *rt) {
    //     static uint8_t ch = 0;
    //     uint16_t raw = hw_mcp3002_read(ch);
    //     adc_data[ch].now_value = raw;
    //     // 最大・最小の更新は同じ
    //     ch ^= 1;   // 0と1を交互に切り替える
    //     return true;
    // }
    // この方法でも動く。ただ、それぞれのチャンネルを読む間隔は、タイマーの間隔の2倍に。
    // ライントレーサーのフォトトランジスタのように、左右の値を同じ瞬間で比べたい場合は、
    // 1の方法のほうが向いている。
}

static repeating_timer_t adc_timer;

#if 0
int main()
{
    mcp3002_init();

    add_repeating_timer_us
    (-1000000, // 1秒ごとにタイマー割り込み
    adc_timer_callback, NULL, &adc_timer);

    while (true) 
    {
    // 1. 割り込みの状態を保存して無効化
    uint32_t ints = save_and_disable_interrupts();
    uint16_t now_raw_0 = adc_data[0].now_value;   // 6つまとめてコピーだけ
    // uint16_t max_raw_0 = adc_data[0].max_value;
    // uint16_t min_raw_0 = adc_data[0].min_value;
    uint16_t now_raw_1 = adc_data[1].now_value;
    // uint16_t max_raw_1 = adc_data[1].max_value;
    // uint16_t min_raw_1 = adc_data[1].min_value;

    // 2. 割り込み状態を元に戻す
    restore_interrupts(ints);
    // save_and_disable_interrupts()で割込みを一瞬止めて、
    // 構造体をローカル変数にコピーしてから表示すると、表示の途中で値が書き換わらない。

    printf("ADC voltage_value_0: %u mV\n", raw_to_mV(now_raw_0));
    printf("ADC voltage_value_1: %u mV\n", raw_to_mV(now_raw_1));
    // printf("ADC voltage_max_value_0: %u mV\n", raw_to_mV(max_raw_0));
    // printf("ADC voltage_min_value_0: %u mV\n", raw_to_mV(min_raw_0));
    // printf("ADC voltage_max_value_1: %u mV\n", raw_to_mV(max_raw_1));
    // printf("ADC voltage_min_value_1: %u mV\n", raw_to_mV(min_raw_1));

    sleep_ms(1000);
    }
}
#endif

#if 1 // === 使用例 ===
#include <stdio.h>
#include "pico/stdlib.h"
#include "adc_mcp3002/adc_mcp3002.h"

int main(void) {
    stdio_init_all();
    mcp3002_init();
    mcp3002_monitor_start(-1000000);   // 1秒ごと

    while (true) {
        MCP3002_DATA_t d0, d1;
        mcp3002_monitor_get(0, &d0);
        mcp3002_monitor_get(1, &d1);
        printf("CH0 now:%u max:%u min:%u mV\n",
               mcp3002_raw_to_mV(d0.now_value), mcp3002_raw_to_mV(d0.max_value), mcp3002_raw_to_mV(d0.min_value));
        printf("CH1 now:%u max:%u min:%u mV\n",
               mcp3002_raw_to_mV(d1.now_value), mcp3002_raw_to_mV(d1.max_value), mcp3002_raw_to_mV(d1.min_value));
        sleep_ms(1000);
    }
}
#endif

