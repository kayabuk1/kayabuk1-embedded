#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// 74HC595をSPI機能を使わずに使う。
// 次、SPI機能を使っていく。↑は一部コメントアウト

#define PIN_MOSI 7
#define PIN_SCK 6
#define PIN_RCK 3

#define MUX_OFF 21
#define MUX_A 20
#define MUX_B 19
#define MUX_C 18

#define SPI_PORT spi0

int main()
{
    stdio_init_all();

    spi_init(SPI_PORT, 1000 * 100);
    // ↑SPI機能の初期設定。SPIインスタンスを渡し、クロック周波数を設定
    // 1000*100=100kHz
    // 1MHzくらいにするとかなりSPI信号波形が鈍ってしまう。
    // 基板上で動かせるのは5MHzが限界とのこと。
    // I2Cは100kHzが限界とのこと。

    // gpioをSPI機能に切り替える。
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_RCK, GPIO_FUNC_SIO);

    gpio_set_function(MUX_OFF, GPIO_FUNC_SIO);
    gpio_set_function(MUX_A, GPIO_FUNC_SIO);
    gpio_set_function(MUX_B, GPIO_FUNC_SIO);
    gpio_set_function(MUX_C, GPIO_FUNC_SIO);

    // ■手動初期設定していたものは、SPI機能が自動で行ってくれる
    // gpio_set_function(PIN_MOSI,GPIO_FUNC_SIO);
    // gpio_set_function(PIN_SCK,GPIO_FUNC_SIO);
    gpio_set_function(PIN_RCK, GPIO_FUNC_SIO);

    // gpio_set_dir(PIN_MOSI, GPIO_OUT);
    // gpio_set_dir(PIN_SCK, GPIO_OUT);
    gpio_set_dir(PIN_RCK, GPIO_OUT);

    gpio_set_dir(MUX_OFF, GPIO_OUT);
    gpio_set_dir(MUX_A, GPIO_OUT);
    gpio_set_dir(MUX_B, GPIO_OUT);
    gpio_set_dir(MUX_C, GPIO_OUT);

    // gpio_init や gpio_set_dir の後に以下を追加
    // gpio_set_slew_rate(PIN_MOSI, GPIO_SLEW_RATE_SLOW); // 変化をゆるやかにする
    // gpio_set_slew_rate(PIN_SCK, GPIO_SLEW_RATE_SLOW);
    // gpio_set_slew_rate(PIN_RCK, GPIO_SLEW_RATE_SLOW);

    // gpio_set_drive_strength(PIN_MOSI, GPIO_DRIVE_STRENGTH_2MA); // 出力電流を最小(2mA)にする
    // gpio_set_drive_strength(PIN_SCK, GPIO_DRIVE_STRENGTH_2MA);
    // gpio_set_drive_strength(PIN_RCK, GPIO_DRIVE_STRENGTH_2MA);

    // ピンを初期状態全て出力 L に
    // gpio_put(PIN_MOSI, 0);
    // gpio_put(PIN_SCK, 0);
    gpio_put(PIN_RCK, 0);

    gpio_put(MUX_OFF, 0);

    while (true)
    {   
        gpio_put(MUX_OFF, 0);

        // // １．クロックを作る
        // gpio_put(PIN_SCK, 0);
        // sleep_us(10);
        //     // ここでデータを読み込ませる。
        //     gpio_put(PIN_MOSI, 1);
        //     sleep_us(10);
        // // 立ち上がりでPIN_MOSのデータが読み込まれる。
        // gpio_put(PIN_SCK, 1);
        // sleep_us(10);
        // // これを四回繰り返す。

        // // １．クロックを作る
        // gpio_put(PIN_SCK, 0);
        // sleep_us(10);
        //     // ここでデータを読み込ませる。
        //     gpio_put(PIN_MOSI, 0);
        //     sleep_us(10);
        // // 立ち上がりでPIN_MOSのデータが読み込まれる。
        // gpio_put(PIN_SCK, 1);
        // sleep_us(10);

        // // １．クロックを作る
        // gpio_put(PIN_SCK, 0);
        // sleep_us(10);
        //     // ここでデータを読み込ませる。
        //     gpio_put(PIN_MOSI, 1);
        //     sleep_us(10);
        // // 立ち上がりでPIN_MOSのデータが読み込まれる。
        // gpio_put(PIN_SCK, 1);
        // sleep_us(10);

        // // １．クロックを作る
        // gpio_put(PIN_SCK, 0);
        // sleep_us(10);
        //     // ここでデータを読み込ませる。
        //     gpio_put(PIN_MOSI, 0);
        //     sleep_us(10);
        // // 立ち上がりでPIN_MOSのデータが読み込まれる。
        // gpio_put(PIN_SCK, 1);
        // sleep_us(10);

        uint8_t data = 0xCC;
        spi_write_blocking(SPI_PORT, &data, 1);
        // 最後にラッチ、データをｼﾌﾄﾚｼﾞｽﾀに読み込ませる。
        gpio_put(PIN_RCK, 1);
        sleep_us(10);
        gpio_put(PIN_RCK, 0);
        sleep_us(10);
        // sleep_ms(100);

        gpio_put(MUX_A, 0);
        gpio_put(MUX_B, 0);
        gpio_put(MUX_C, 0);

        gpio_put(MUX_OFF, 1);

        sleep_us(100);

    }
}
