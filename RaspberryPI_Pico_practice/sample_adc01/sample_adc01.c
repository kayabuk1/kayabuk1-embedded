#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

int main()
{
    adc_init();
    stdio_init_all();
    adc_gpio_init(27);
    adc_gpio_init(4);
    // 👇select ADC input 1 (GPIO 27)を設定するという意味
    adc_select_input(1);
    // 2の12bit分解能でADCを設定されている
    //  ＝4096段階で分解能を表すことができる。
    // なので12ビット以上あるデータ型ならADC変換の結果を格納できる。
    uint16_t raw_gp27;
    uint16_t raw_temp;

    const float conversion_factor = 3.3f / (1 << 12);

    adc_set_temp_sensor_enabled(true);

    while (true)
    {
        adc_select_input(1);
        raw_gp27 = adc_read();
        // 👆adc_read()は、adcピンの値を読み取る関数。
        // 戻り値データ型は、uint16_t型なので格納する変数のデータ型も
        // それに合わせて変更。
        printf("ADC raw_value: %f\n", raw_gp27 * conversion_factor);

        adc_select_input(4);
        raw_temp = adc_read();
        printf("ADC temp_sensor: %f\n", raw_temp * conversion_factor);
        printf("------------------------------------\n");
        sleep_ms(1000);
    }
}