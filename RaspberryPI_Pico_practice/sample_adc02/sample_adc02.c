#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

int main()
{
    stdio_init_all();
    adc_init();
    // ↓内部温度センサーも有効化する
    adc_set_temp_sensor_enabled(true);
    // ↓ADC対象に４番を指定する
    adc_select_input(4);

    while (true) {
        const float conversion_factor = 3.3f / (1 << 12);
        uint16_t raw_val = adc_read();
        float voltage = raw_val * (3.3f / 4096.0f);
        float temperature 
            = 27.0f - (voltage - 0.706f) / 0.001721f;
        printf(
            "Raw value: %03d, voltage: %f V\n",
             raw_val, raw_val * conversion_factor);
        printf("Temperature: %.2f deg C\n", temperature);
        sleep_ms(1000);
    }
}
/*
Raw value: 0x37e, voltage: 0.720264 V
Raw value: 0x37e, voltage: 0.720264 V
Raw value: 0x37e, voltage: 0.720264 V
Raw value: 0x37e, voltage: 0.720264 V
Raw value: 0x37e, voltage: 0.720264 V
Raw value: 0x37e, voltage: 0.720264 V

Temperature: 20.58 deg C
Raw value: 890, voltage: 0.717041 V
Temperature: 20.58 deg C
Raw value: 891, voltage: 0.717847 V
Temperature: 20.12 deg C
*/