#ifndef MCP3002_H
#define MCP3002_H
#include <stdint.h> 
#include <stdbool.h>
#include "hardware/spi.h"

// ========= MCP3002 配線表 =========
// 1 | !CS/SHDN      ⇒ GPIO3
// 2 | CH0           ⇒ 測定したいもの（可変抵抗の2番端子など）
// 3 | CH1           ⇒ 測定したいもの
// 4 | VSS(GND)
// 5 | D_IN (MOSI)   ⇒ GPIO7
// 6 | D_OUT(MISO)   ⇒ GPIO4
// 7 | CLK  (SCK)    ⇒ GPIO6
// 8 | VDD/VREF      ⇒ 3.3V（この電圧がそのまま基準電圧になる）

#define MCP3002_SPI_PORT   spi0
#define MCP3002_PIN_MISO   4
#define MCP3002_PIN_CS     3
#define MCP3002_PIN_SCK    6
#define MCP3002_PIN_MOSI   7
#define MCP3002_BAUDRATE   (1000 * 1000)
#define MCP3002_VREF_MV    3300
#define MCP3002_RESOLUTION 1024
#define MCP3002_CH_NUM     2

typedef struct {
    uint16_t max_value;
    uint16_t min_value;
    uint16_t now_value;
} MCP3002_DATA_t;

// ----- ドライバ部分 -----
void     mcp3002_init(void);
uint16_t mcp3002_read(uint8_t channel);   // raw（0〜1023）を返す

static inline uint16_t mcp3002_raw_to_mV(uint16_t raw) {
    return (uint16_t)(((uint32_t)raw * MCP3002_VREF_MV) / MCP3002_RESOLUTION);
}

// ----- タイマー割込みで定期的に読む部分 -----
bool mcp3002_monitor_start(int64_t interval_us);              // 例：-10000で10msごと
void mcp3002_monitor_get(uint8_t channel, MCP3002_DATA_t *out);

#endif

#if 0 // === 使用例 ===
#include <stdio.h>
#include "pico/stdlib.h"
#include "adc_mcp3002/adc_mcp3002.h"

int main(void) {
    stdio_init_all();
    mcp3002_init();
    mcp3002_monitor_start(-1000000);   // 1秒ごと

    while (true) {
        mcp3002_DATA_t d0, d1;
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