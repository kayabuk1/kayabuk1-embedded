#include "pico/stdlib.h"

void hardware_setup(void)
{

    gpio_set_function(10, GPIO_FUNC_SIO);
    gpio_set_function(11, GPIO_FUNC_SIO);
    gpio_set_function(12, GPIO_FUNC_SIO);

    gpio_set_function(13, GPIO_FUNC_SIO);
    gpio_set_function(14, GPIO_FUNC_SIO);
    gpio_set_function(15, GPIO_FUNC_SIO);

    gpio_set_dir(10, GPIO_OUT);
    gpio_set_dir(11, GPIO_OUT);
    gpio_set_dir(12, GPIO_OUT);

    gpio_set_dir(13, GPIO_IN);
    gpio_set_dir(14, GPIO_IN);
    gpio_set_dir(15, GPIO_IN);
    gpio_pull_up(13);
    gpio_pull_up(14);
    gpio_pull_up(15);
}