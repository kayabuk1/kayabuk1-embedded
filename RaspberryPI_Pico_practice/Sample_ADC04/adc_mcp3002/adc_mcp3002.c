#include "adc_mcp3002.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

// ================= ドライバ部分 =================

void mcp3002_init(void) {
    spi_init(MCP3002_SPI_PORT, MCP3002_BAUDRATE);
    spi_set_format(MCP3002_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(MCP3002_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MCP3002_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(MCP3002_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_init(MCP3002_PIN_CS);
    gpio_set_dir(MCP3002_PIN_CS, GPIO_OUT);
    gpio_put(MCP3002_PIN_CS, 1);
}

uint16_t mcp3002_read(uint8_t channel) {
    if (channel >= MCP3002_CH_NUM) channel = 0;
    uint8_t tx_buf[2] = {0b01101000 | ((channel & 0x01) << 4), 0x00};
    uint8_t rx_buf[2] = {0};
    gpio_put(MCP3002_PIN_CS, 0);
    spi_write_read_blocking(MCP3002_SPI_PORT, tx_buf, rx_buf, 2);
    gpio_put(MCP3002_PIN_CS, 1);
    return ((rx_buf[0] & 0x03) << 8) | rx_buf[1];
}

// ============ タイマー割込みで定期的に読む部分 ============

static volatile MCP3002_DATA_t mcp3002_data[MCP3002_CH_NUM] 
= {
    {0, MCP3002_RESOLUTION - 1, 0},
    {0, MCP3002_RESOLUTION - 1, 0},
};
static repeating_timer_t mcp3002_timer;

static bool mcp3002_timer_callback(__unused repeating_timer_t *rt)
{
    for (uint8_t ch = 0; ch < MCP3002_CH_NUM; ch++) {
        uint16_t raw = mcp3002_read(ch);
        mcp3002_data[ch].now_value = raw;
        if (raw > mcp3002_data[ch].max_value) mcp3002_data[ch].max_value = raw;
        if (raw < mcp3002_data[ch].min_value) mcp3002_data[ch].min_value = raw;
    }
    return true;
}

bool mcp3002_monitor_start(int64_t interval_us) {
    return add_repeating_timer_us(
        interval_us, mcp3002_timer_callback, NULL, &mcp3002_timer);
}

void mcp3002_monitor_get(uint8_t channel, MCP3002_DATA_t *out) {
    if (channel >= MCP3002_CH_NUM) channel = 0;
    uint32_t ints = save_and_disable_interrupts();
    out->now_value = mcp3002_data[channel].now_value;
    out->max_value = mcp3002_data[channel].max_value;
    out->min_value = mcp3002_data[channel].min_value;
    restore_interrupts(ints);
}