#ifndef MCP3208or3008_H
#define MCP3208or3008_H
#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"
#include "board.h"

// ========= MCP3208or3008 のピン（2026年モデルの基板。仕様書 表1-5） =========
// *　１〜８｜CH0〜CH7 ⇒ ラインセンサー6個（CH0〜CH5）、マーカーセンサー2個（CH6、CH7）
// *　９  ｜DGND
// *　１０｜!CS/SHDN ⇒ GPIO14
// *　１１｜DIN(MOSI) ⇒ GPIO11（L3GD20 と共用）
// *　１２｜DOUT(MISO) ⇒ GPIO12（L3GD20 と共用）
// *　１３｜CLK(SCLK) ⇒ GPIO10（L3GD20 と共用）
// *　１４｜AGND
// *　１５｜VREF ⇒ 3.3V
// *　１６｜VDD ⇒ 3.3V
// ピンの番号は board.h にあります。ブレッドボードで試すときは、board.h の値を変えます。

#define MCP3208or3008_SPI_PORT   BOARD_SPI1_PORT
#define MCP3208or3008_PIN_MISO   PIN_SPI1_MISO
#define MCP3208or3008_PIN_CS     PIN_ADC_CS
#define MCP3208or3008_PIN_SCK    PIN_SPI1_SCK
#define MCP3208or3008_PIN_MOSI   PIN_SPI1_MOSI
#define MCP3208or3008_BAUDRATE   (1000 * 1000)
#define MCP3208or3008_VREF_MV    3300

// 読み取る値のビット数です。機体の基板は MCP3008（10ビット）なので、10 にしてあります（仕様書 表1-2）。
// 送る3バイトは、MCP3008 でも MCP3208 でも同じです。返ってくる12ビット分の並びが違います。
//   MCP3008 : 上の10ビットが値で、下の2ビットは値ではない（12ビットとして読むと、約4倍の値になる）
//   MCP3208 : 12ビット全部が値
// mcp3208or3008_read() は、12ビット分を取り出してから、右に (12 - ビット数) だけずらします。
//   10 のとき : MCP3008 は値そのまま。MCP3208 でも、上の10ビット（0〜1023）として読めます
//   12 のとき : MCP3208 の値そのまま（0〜4095）。MCP3008 には使いません
#ifndef MCP3208or3008_BITS
#define MCP3208or3008_BITS       10
#endif
#define MCP3208or3008_RESOLUTION (1u << MCP3208or3008_BITS) // 10ビットなら 1024、12ビットなら 4096
#define MCP3208or3008_CH_NUM     8
#define MCP3208or3008_DEBUG      0

typedef struct {
    uint16_t max_value;
    uint16_t min_value;
    uint16_t now_value;
} MCP3208or3008_DATA_t;

// ----- ドライバ部分 -----
void     mcp3208or3008_init(void);
uint16_t mcp3208or3008_read(uint8_t channel);
// raw（10ビットなら 0〜1023、12ビットなら 0〜4095）を返す

static inline uint16_t mcp3208or3008_raw_to_mV(uint16_t raw)
{
    return (uint16_t)(((uint32_t)raw * MCP3208or3008_VREF_MV)
     / MCP3208or3008_RESOLUTION);
}

// ----- タイマー割込みで定期的に読む部分 -----
// 機体のプログラムでは使いません（1msの制御の割り込みが、mcp3208or3008_read() を直接呼びます）。
// 単体で試すときのために、残してあります。
bool mcp3208or3008_monitor_start(int64_t interval_us);
// 例：-10000で10msごと
void mcp3208or3008_monitor_get(
    uint8_t channel, MCP3208or3008_DATA_t *out);

#endif
