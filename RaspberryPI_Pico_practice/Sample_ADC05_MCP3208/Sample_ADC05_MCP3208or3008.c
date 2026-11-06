#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "adc_mcp3208or3008/adc_mcp3208or3008.h"

#define MCP3208or3008_PIN_MOSI 7 //MCP3208or3008 物理ピン11番DIN 
#define MCP3208or3008_PIN_MISO 4 //MCP3208or3008 物理ピン12番DOUT
#define MCP3208or3008_PIN_CS 3 //MCP3208or3008 物理ピン10番!CS/SHDN(ﾁｯﾌﾟｾﾚｸﾄ/ｼｬｯﾄﾀﾞｳﾝ入力)
// ↑※通信相手指定アクティブロー
#define MCP3208or3008_PIN_SCK 6 //MCP3208or3008 物理ピン13番CLK
#define MCP3208or3008_SPI_PORT spi0 //picoに2つあるSPI通信ｺﾝﾄﾛｰﾗの1つ目を使うよ、と指定。
#define MCP3208or3008_VREF_MV 3300 //⚠PicoのGPIOは3.3Vまでなので、基準電圧は3.3Vとする。
#define MCP3208or3008_RESOLUTION 4096 // 12bitADCなので、2^12=4096
#define MCP3208or3008_BAUDRATE   (1000 * 1000)
#define MCP3208or3008_DEBUG 0 // デバッグ用のprintf()を有効にするかどうかのフラグ

// ========= MCP3208or3008配線表※ピン配置同じ（ピン割り当て表） =====================
// -－ピン割り当て表
// *　１｜CH0 ⇒ 可変抵抗1の2番端子(真ん中)※電圧を測定したいものを接続
// *　２｜CH1 ⇒ 可変抵抗2
// *　３｜CH2 ⇒ 可変抵抗3
// *　４｜CH3 ⇒ 可変抵抗4
// *　５｜CH4 ⇒ 可変抵抗5
// *　６｜CH5 ⇒ 可変抵抗6
// *　７｜CH6 ⇒ 可変抵抗6の2番端子(真ん中)※CH5と同じ電圧を測定
// *　８｜CH7 ⇒ 可変抵抗5の2番端子(真ん中)※CH4と同じ電圧を測定
// ‐‐‐―――折り返し
// *　９  ｜DGND(ﾃﾞｼﾞﾀﾙｸﾞﾗｳﾝﾄﾞ※内部ﾃﾞｼﾞﾀﾙ回路用接地)。picoとGNDを共通にする。
// *　１０｜!CS/SHDN(ﾁｯﾌﾟｾﾚｸﾄ/ｼｬｯﾄﾀﾞｳﾝ入力) ⇒ GPIO3
// *　１１｜DIN(MOSI) ⇒ GPIO7
// *　１２｜DOUT(MISO) ⇒ GPIO4
// *　１３｜CLK(SCLK) ⇒ GPIO6
// *　１４｜AGND(ｱﾅﾛｸﾞｸﾞﾗｳﾝﾄﾞ※内部ｱﾅﾛｸﾞ回路用接地)。picoとGNDを共通にする。
// *　１５｜VREF(基準電圧入力) ⇒ 3.3V
// *　１６｜VDD(電源電圧) ⇒ 3.3V

typedef struct ADC_DATA_t
{
    /* data */
    uint16_t max_value;
    uint16_t min_value;
    uint16_t now_value;
} ADC_DATA_t;

static volatile ADC_DATA_t adc_data[8] 
= {
    {0, MCP3208or3008_RESOLUTION-1, 0},
    {0, MCP3208or3008_RESOLUTION-1, 0},
    {0, MCP3208or3008_RESOLUTION-1, 0},
    {0, MCP3208or3008_RESOLUTION-1, 0},
    {0, MCP3208or3008_RESOLUTION-1, 0},
    {0, MCP3208or3008_RESOLUTION-1, 0},
    {0, MCP3208or3008_RESOLUTION-1, 0},
    {0, MCP3208or3008_RESOLUTION-1, 0}
}; 
// ADCデータ構造体の初期化 {0,4095,0} → {0, MCP3208or3008_RESOLUTION-1, 0} に変更。
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
 * @param raw 12ビットのデジタル出力コード（0～4095）
 * @return uint16_t mV単位の電圧値
 */
static inline uint16_t raw_to_mV(uint16_t raw) 
{
    return 
    (uint16_t)(((uint32_t)raw * 
    MCP3208or3008_VREF_MV) / MCP3208or3008_RESOLUTION);    // mV単位で計算
    // MCP3208or3008から得られた12ビットのデジタル出力コード（0～4095）を、
    // 実際の入力アナログ電圧V_in(V)に直す。
    // 1LSB(1bit辺りの電圧変化量)=V_ref / 2^12 = 3.3V /
    //  4096 = 0.0008056640625V 
    // ≒ 0.81mV
    // V_in = (raw_value / 4096.0) *16 3.3V

    // PicoのRP2040には浮動小数点を専用に計算する回路（FPU）がないので、
    // 小数の計算はソフトで行われて少し重くなる。
    // 先に掛けてから割るのがコツ。4095 × 3300 = 13,513,500で、
    // 16ビット（最大65,535）を超えるので、uint32_tに広げてから掛ける。
}

#if 0
/**
 * @brief ハードウェア初期化処理
 */
void MCP3208or3008_init(void)
{
    stdio_init_all();

    spi_init(MCP3208or3008_SPI_PORT, MCP3208or3008_BAUDRATE); 
    //SPI通信の初期化。通信速度は1MHzに設定。
    // ●ﾃﾞｰﾀｼｰﾄ記載
    // Timing Parameters:
    // Clock Frequency fCLK 
    // 3.2MHZ VDD = 5V (Note 2)
    // 1.2MHz VDD = 2.7V (Note 2) より 1MHzで問題なし。
    // 8bit/1000*1000※1MHz = 0.000_008秒=8us
    spi_set_format (
        MCP3208or3008_SPI_PORT,       // spi_inst_t SPIインスタンス (spi0 または spi1)
        8,        // uint 1回あたりのデータビット数 (通常は 4 〜 16)
        SPI_CPOL_0,       // spi_cpol_t クロック極性 (SPI_CPOL_0 または SPI_CPOL_1)
        SPI_CPHA_0,       // spi_cpha_t クロック位相 (SPI_CPHA_0 または SPI_CPHA_1)
        SPI_MSB_FIRST 
    // __unused spi_order_t ビットオーダー (SPI_MSB_FIRST または SPI_LSB_FIRST)
    // ※RP2040はMSBファーストにしか対応していない。
    );

    // SPI通信用ピン機能設定
    gpio_set_function(MCP3208or3008_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MCP3208or3008_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(MCP3208or3008_PIN_SCK,  GPIO_FUNC_SPI); 
    gpio_set_function(MCP3208or3008_PIN_CS,  GPIO_FUNC_SIO);
    
    // ピン方向を出力に設定
    gpio_set_dir(MCP3208or3008_PIN_CS, GPIO_OUT);
    gpio_put(MCP3208or3008_PIN_CS, 1);
    // ローアクティブの為、CSピンをHIGHにしておく。通信開始時にLOWにする。
}
#endif

/**
 * @brief MCP3208or3008からADC値を読み取る関数
 * @param channel チャンネル番号 (0 to 7)
 * @return uint16_t ADC値
 */
uint16_t hw_mcp3208or3008_read(uint8_t channel)
{   
    if (channel > 7) {
        #if DEBUG
        printf("hw_mcp3208or3008_read: channel out of range, set to 0\n");
        #endif
        return 0; // チャンネル番号が範囲外の場合は0を返す
    }
    uint8_t tx_buf[3] = {0};
    uint8_t rx_buf[3] = {0};

    // ◆MCP3208の制御ビット（入力チャネル選択テーブル）
    // コマンド送信時、SGL/DIFF ビットと 3つの選択ビット（D2, D1, D0）を
    // 組み合わせる。
    // SGL/DIFF　D2　D1　D0　動作モード　測定対象ピン
    // 1　　　　　0　 0　 0　 シングルエンドCH0 (対 GND)
    // 1　　　　　0　 0　 1　 シングルエンドCH1 (対 GND)
    // 1　　　　　0　 1　 0　 シングルエンドCH2 (対 GND)
    // 1　　　　　0　 1　 1　 シングルエンドCH3 (対 GND)
    // 1　　　　　1　 0　 0　 シングルエンドCH4 (対 GND)
    // 1　　　　　1　 0　 1　 シングルエンドCH5 (対 GND)
    // 1　　　　　1　 1　 0　 シングルエンドCH6 (対 GND)
    // 1　　　　　1　 1　 1　 シングルエンドCH7 (対 GND)

    // [BIT1]Start (1)
    // [BIT2]SGL/DIFF (1)//single-ended-mode選択
    // [BIT3]D2 (x)
    // [BIT4]D1 (x)
    // [BIT5]D0 (x)
    // よって、
    // 0b11xxx000 0b0000000 0b00000000
    // 返ってくるデータは、12bitのADC値 + NULLbitなので、13bit必要。
    // 0b000nxxxx_xxxxxxxxの形で返ってくる。
    // 送るときにBIT18の位置にスタートビットが来るようにすると、制御ビットを
    // 送ってから、すぐに13bitのデータを受信できる。
    // よって、
    // 0b000000SS_DDDn0000_00000000
    tx_buf[0] = 0b00000110 | ((channel & 0x04)>>2); // 送信バッファの初期化
    tx_buf[1] = 0b00000000 | ((channel & 0x03) << 6); 
    // ※ tx_buf[1] は 0x00 のまま（Don't Careのダミーバイトとして送信）
    // ●MCP3208or3008の!CSへチャンネル選択Low信号入力後の制御ビットの構造
    // ・Start bit（スタートビット）: 最初に入力される High（1）のビット。
    // これを受信するとICは通信開始と認識する。
    // ・SGL/DIFF: シングルエンドモード（1）か、疑似差動モード（0）かを選択。
    // ・ODD/SIGN: チャンネルの選択および極性を指定。
    // 今回はVss(GND)を基準に電圧を比較するシングルエンドモードで、
    // CH0を選択するため 0 に設定。
    // ・MSBF: 出力データフォーマットを指定。1 に設定すると変換結果を MSBファースト
    // （最上位ビットから順）で受信。

    gpio_put(MCP3208or3008_PIN_CS, 0); // CSをLOWにして通信開始
    spi_write_read_blocking(MCP3208or3008_SPI_PORT, tx_buf, rx_buf, 3);
    gpio_put(MCP3208or3008_PIN_CS, 1); // CSをHIGHにして通信終了
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

    uint16_t raw_value = (rx_buf[1] &0x0F) <<8 | rx_buf[2];
    return raw_value;
    // 12ビットのデータを取得。
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
    for (uint8_t ch = 0; ch < 8; ch++) {
    // ●一回のコールバックで、CH0とCH1を続けて読む。
    // 一回の読み取りは1MHzで24クロック、約24µs。
    // 24*8=192us。1回のｺｰﾙﾊﾞｯｸほぼ200us掛かる。
    // タイマーの間隔は、これより十分長くする（1秒なら問題なし）。

        uint16_t raw = hw_mcp3208or3008_read(ch); // 今のCHのADC値を取得して代入。
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
    //     uint16_t raw = hw_mcp3208or3008_read(ch);
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

#if 1 // === 使用例 ===
#include <stdio.h>
#include "pico/stdlib.h"
#include "adc_mcp3208or3008/adc_mcp3208or3008.h"

int main(void) {
    stdio_init_all();
    mcp3208or3008_init();
    mcp3208or3008_monitor_start(-1000000);   // 1秒ごと

    while (true) {
        for (uint8_t ch=0; ch < MCP3208or3008_CH_NUM; ch++){
            MCP3208or3008_DATA_t d = {0};
            mcp3208or3008_monitor_get(ch,&d);
            printf("CH%u now:%u max:%u min:%u mV\n",ch,
                mcp3208or3008_raw_to_mV(d.now_value),
                    mcp3208or3008_raw_to_mV(d.max_value),
                    mcp3208or3008_raw_to_mV(d.min_value));      
        }

       sleep_ms(1000);
    }
}
#endif

