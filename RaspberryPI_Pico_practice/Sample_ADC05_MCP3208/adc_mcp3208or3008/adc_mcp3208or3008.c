#include "adc_mcp3208or3008.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <stdio.h>

// ================= ドライバ部分 =================

void mcp3208or3008_init(void) {
    spi_init(MCP3208or3008_SPI_PORT, MCP3208or3008_BAUDRATE);
    spi_set_format(
        MCP3208or3008_SPI_PORT, 8, SPI_CPOL_0
        , SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(MCP3208or3008_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MCP3208or3008_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(MCP3208or3008_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_init(MCP3208or3008_PIN_CS);
    gpio_set_dir(MCP3208or3008_PIN_CS, GPIO_OUT);
    gpio_put(MCP3208or3008_PIN_CS, 1);
}

uint16_t mcp3208or3008_read(uint8_t channel) {
    if (channel >= MCP3208or3008_CH_NUM) channel = 0;
    uint8_t tx_buf[3] = {0b00000110 | ((channel & 0x04)>>2), (channel & 0x03) << 6, 0x00};
    uint8_t rx_buf[3] = {0};
    gpio_put(MCP3208or3008_PIN_CS, 0);
    spi_write_read_blocking
    (MCP3208or3008_SPI_PORT, tx_buf, rx_buf, 3);
    gpio_put(MCP3208or3008_PIN_CS, 1);

    #if DEBUG // 受信データ3byteの様子を確認したい時用。
    printf("%02X %02X %02X\n", rx_buf[0], rx_buf[1], rx_buf[2]);
    #endif

    return ((rx_buf[1] & 0x0F) << 8) | rx_buf[2];
}

// ============ タイマー割込みで定期的に読む部分 ============

static volatile MCP3208or3008_DATA_t 
mcp3208or3008_data[MCP3208or3008_CH_NUM] 
= {
    {0, MCP3208or3008_RESOLUTION - 1, 0},
    {0, MCP3208or3008_RESOLUTION - 1, 0},
    {0, MCP3208or3008_RESOLUTION - 1, 0},
    {0, MCP3208or3008_RESOLUTION - 1, 0},
    {0, MCP3208or3008_RESOLUTION - 1, 0},
    {0, MCP3208or3008_RESOLUTION - 1, 0},
    {0, MCP3208or3008_RESOLUTION - 1, 0},
    {0, MCP3208or3008_RESOLUTION - 1, 0},
};
static repeating_timer_t mcp3208or3008_timer;

static bool mcp3208or3008_timer_callback(
    __unused repeating_timer_t *rt)
{
    for (uint8_t ch = 0; ch < MCP3208or3008_CH_NUM; ch++) 
    {
        uint16_t raw = mcp3208or3008_read(ch);
        mcp3208or3008_data[ch].now_value = raw;
        if (raw > mcp3208or3008_data[ch].max_value) 
        {
            mcp3208or3008_data[ch].max_value = raw;
        }
        if (raw < mcp3208or3008_data[ch].min_value) 
        {
            mcp3208or3008_data[ch].min_value = raw;
        }
    }
    return true;
}

bool mcp3208or3008_monitor_start(int64_t interval_us) {
    return add_repeating_timer_us(
        interval_us, mcp3208or3008_timer_callback, NULL, &mcp3208or3008_timer);
}

void mcp3208or3008_monitor_get(
    uint8_t channel, MCP3208or3008_DATA_t *out) {
    if (channel >= MCP3208or3008_CH_NUM) return;
    uint32_t ints = save_and_disable_interrupts();
    out->now_value = mcp3208or3008_data[channel].now_value;
    out->max_value = mcp3208or3008_data[channel].max_value;
    out->min_value = mcp3208or3008_data[channel].min_value;
    restore_interrupts(ints);
}