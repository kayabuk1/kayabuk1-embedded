#include <stdio.h>
#include "pico/stdlib.h"
#include "pico_hardware_setup.h"

int main()
{
    stdio_init_all();
    hardware_setup();

    while (true)
    {
        gpio_put(10, !gpio_get(13));
        gpio_put(11, !gpio_get(14));
        gpio_put(12, !gpio_get(15));
    }
}