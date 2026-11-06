#ifndef MCP3208or3008_H
#define MCP3208or3008_H
#include <stdint.h> 
#include <stdbool.h>
#include "hardware/spi.h"

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

#define MCP3208or3008_SPI_PORT   spi0
#define MCP3208or3008_PIN_MISO   4
#define MCP3208or3008_PIN_CS     3
#define MCP3208or3008_PIN_SCK    6
#define MCP3208or3008_PIN_MOSI   7
#define MCP3208or3008_BAUDRATE   (1000 * 1000)
#define MCP3208or3008_VREF_MV    3300
#define MCP3208or3008_RESOLUTION 4096
#define MCP3208or3008_CH_NUM     8
#define DEBUG 0

typedef struct {
    uint16_t max_value;
    uint16_t min_value;
    uint16_t now_value;
} MCP3208or3008_DATA_t;

// ----- ドライバ部分 -----
void     mcp3208or3008_init(void);
uint16_t mcp3208or3008_read(uint8_t channel);   
// raw（0〜4095）を返す

static inline uint16_t mcp3208or3008_raw_to_mV(uint16_t raw) 
{
    return (uint16_t)(((uint32_t)raw * MCP3208or3008_VREF_MV)
     / MCP3208or3008_RESOLUTION);
}

// ----- タイマー割込みで定期的に読む部分 -----
bool mcp3208or3008_monitor_start(int64_t interval_us);  
// 例：-10000で10msごと
void mcp3208or3008_monitor_get(
    uint8_t channel, MCP3208or3008_DATA_t *out);

#endif

#if 0 // === 使用例 ===
#include <stdio.h>
#include "pico/stdlib.h"
#include "adc_mcp3208or3008/adc_mcp3208or3008.h"

int main(void) {
    stdio_init_all();
    mcp3208or3008_init();
    mcp3208or3008_monitor_start(-1000000);   // 1秒ごと

    while (true) {
        MCP3208or3008_DATA_t d[MCP3208or3008_CH_NUM] = {0};
        for (uint8_t ch=0; ch <8; ch++){
        mcp3208or3008_monitor_get(ch,&d[ch]);
        printf("CH%u now:%u max:%u min:%u mV\n",ch,
               mcp3208or3008_raw_to_mV(d[ch].now_value),
                mcp3208or3008_raw_to_mV(d[ch].max_value),
                 mcp3208or3008_raw_to_mV(d[ch].min_value));      
        }

       sleep_ms(1000);
    }
}
#endif