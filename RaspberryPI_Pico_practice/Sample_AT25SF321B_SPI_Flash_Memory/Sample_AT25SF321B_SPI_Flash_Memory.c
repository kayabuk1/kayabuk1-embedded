#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define Normal_Read 0x03
#define Fast_Read 0x0B



int main()
{
    stdio_init_all();

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
