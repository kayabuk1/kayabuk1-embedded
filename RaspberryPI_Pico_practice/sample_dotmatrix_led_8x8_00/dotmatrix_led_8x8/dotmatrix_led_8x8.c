#include <stdint.h>
#include "pico/stdlib.h"
#include "dotmatrix_led_8x8.h"
#include "pico/stdlib.h"

void init_spi(void)
// ハードウェアセットアップどは別にSPI機能の初期化は、
// こちらにまとめられている。
{
    // SPI初期化
    spi_init(SPI_PORT, 1 * 1000 * 1000); // 1 MHz

    // SPIのピン設定
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

    // LATCH
    gpio_set_function(PIN_LATCH, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_LATCH, GPIO_OUT);
    gpio_put(PIN_LATCH, 0);
}

// 74HC595へ1バイト送信
void hc595_write(uint8_t data)
{
    // ビット反転（74HC595でLEDを
    // ドライブしているが、Lアクティブでの駆動のため）
    data = ~data;
    // シリアルデータを送信
    spi_write_blocking(SPI_PORT, &data, 1);

    // RCLKをHにしてラッチ
    gpio_put(PIN_LATCH, 1);
    sleep_us(1);
    gpio_put(PIN_LATCH, 0);
}
