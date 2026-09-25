#include <stdio.h>
#include "pico/stdlib.h"
#include "pico_hardware_setup.h"
#include "pico_led_sw.h"

int main()
{
    stdio_init_all();
    hardware_setup();

    while (true)
    {   
        // out_led0(get_tactSW0());
        // out_led1(get_tactSW1());
        // out_led2(get_tactSW2());
        out_led_byte(get_tactSW_byte());

        // out_led_byte(0x01);
        // sleep_ms(300);
        // out_led_byte(0x02);
        // sleep_ms(300);
        // out_led_byte(0x04);
        // sleep_ms(300);
        // out_led_byte(0x00);
        // sleep_ms(300);
        // out_led_byte(0x07);
        // sleep_ms(300);

        // out_led0(true);
        // sleep_ms(500);
        // out_led0(false);
        // sleep_ms(500);
        // out_led1(true);
        // sleep_ms(500);
        // out_led1(false);
        // sleep_ms(500);
        // out_led2(true);
        // sleep_ms(500);
        // out_led2(false);
        // sleep_ms(500);
    }
}
