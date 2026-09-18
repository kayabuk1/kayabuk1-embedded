#include <stdio.h>
#include "pico/stdlib.h"
#include "pico_hardware_setup.h"

int main()
{
    stdio_init_all();
    hardware_setup();

    while (true) {
        printf("%d\n",gpio_get(13));
        // printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
