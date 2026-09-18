#include <stdio.h>
#include "pico/stdlib.h"
#include <stdlib.h>

int main()
{
    stdio_init_all();
    // gpio_set_function(10, GPIO_FUNC_SIO);
    // gpio_init(10);；↑はこれと同等。
    // gpio_init(11);
    // gpio_init(12);
    gpio_set_function_masked(
        1 << 12 | 1 << 11 | 1 << 10, GPIO_FUNC_SIO);
    gpio_set_dir(10, GPIO_OUT);
    gpio_set_dir(11, GPIO_OUT);
    gpio_set_dir(12, GPIO_OUT);

    while (true)
    {
        gpio_put(rand() % 3 + 10, 1);
        sleep_ms(rand() % 300 + 20);
        gpio_put(rand() % 3 + 10, 0);
        sleep_ms(rand() % 300 + 20);
        gpio_put(rand() % 3 + 10, 1);
        sleep_ms(rand() % 300 + 20);
        gpio_put(rand() % 3 + 10, 0);
        sleep_ms(rand() % 300 + 20);
        gpio_put(rand() % 3 + 10, 1);
        sleep_ms(rand() % 300 + 20);
        gpio_put(rand() % 3 + 10, 0);
        sleep_ms(rand() % 300 + 20);
    }
}
#include <stdio.h>
#include "pico/stdlib.h"


int main()
{
    stdio_init_all();
    gpio_set_function(10, GPIO_FUNC_SIO);
    gpio_set_function(11, GPIO_FUNC_SIO);
    gpio_set_function(12, GPIO_FUNC_SIO);

    gpio_set_function(13, GPIO_FUNC_SIO);
    gpio_set_function(14, GPIO_FUNC_SIO);
    gpio_set_function(15, GPIO_FUNC_SIO);

    gpio_set_dir(10, GPIO_OUT);
    gpio_set_dir(11, GPIO_OUT);
    gpio_set_dir(12, GPIO_OUT);

    gpio_set_dir(13, GPIO_OUT);
    gpio_set_dir(14, GPIO_OUT);
    gpio_set_dir(15, GPIO_OUT);

    while (true) {
            gpio_put(10, gpio_get(13));
    }
}