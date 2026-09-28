#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
// ■ADC：ラウンドロビン方式でのADC変換


int main()
{
    stdio_init_all();
    adc_init();
    // ↓gpioの２７番のADCを有効化する
    adc_gpio_init(27);

    // ↓内部温度センサーも有効化する
    adc_set_temp_sensor_enabled(true);
    // ↓ADC対象に４番を指定する
    adc_select_input(4);

    // ↓ラウンドロビンという機能で一括で読み込める。ADC１と４を対象に
    // ADC     ↓gpio27番
    // ４３２１０ビット
    // 1:0:0:1:0
    // という風にビットを立てる。
    adc_set_round_robin(0x12);
    // ⚠４ビット目から読まれない様に、最初の読み込みADC対象を１にする
    adc_select_input(1);

    while (true) {

        uint16_t raw_val_1 = adc_read();
        uint16_t raw_val_2 = adc_read();

        const float conversion_factor = 3.3f / (1 << 12);
        
        printf("ADC_1:%u ADC_4:%u\n"
            ,raw_val_1,raw_val_2
            );
        sleep_ms(1000);
    }
}
/*
ADC_1:5741 ADC_4:890
ADC_1:5754 ADC_4:890
ADC_1:5742 ADC_4:890

ADC_1:4095 ADC_4:886
ADC_1:3616 ADC_4:888
ADC_1:3121 ADC_4:888
ADC_1:3121 ADC_4:886
ADC_1:3121 ADC_4:886
*/