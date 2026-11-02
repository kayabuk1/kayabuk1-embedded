#include "pico/stdlib.h"

void hardware_setup(void)
{
    stdio_init_all();

    gpio_set_function(10, GPIO_FUNC_SIO);
    gpio_set_function(11, GPIO_FUNC_SIO);
    gpio_set_function(12, GPIO_FUNC_SIO);

    gpio_set_function(13, GPIO_FUNC_SIO);
    gpio_set_function(14, GPIO_FUNC_SIO);
    gpio_set_function(15, GPIO_FUNC_SIO);

    gpio_set_function(25, GPIO_FUNC_SIO);

    gpio_set_dir(10, GPIO_OUT);
    gpio_set_dir(11, GPIO_OUT);
    gpio_set_dir(12, GPIO_OUT);

    // Picoボード上のSW0,1,2の入力を受け付けるための準備
    gpio_set_dir(13, GPIO_IN);
    gpio_set_dir(14, GPIO_IN);
    gpio_set_dir(15, GPIO_IN);

    // Pico基板上のLEDの制御
    gpio_set_dir(25, GPIO_OUT);
   
    // SWに接続されているGPIOのpull up抵抗をONに。
    gpio_pull_up(13);
    gpio_pull_up(14);
    gpio_pull_up(15);

    //dotmatrix layer 
    gpio_set_function(19, GPIO_FUNC_SIO);
    gpio_set_function(20, GPIO_FUNC_SIO);
    gpio_set_function(21, GPIO_FUNC_SIO);
    gpio_set_dir(19, GPIO_OUT);
    gpio_set_dir(20, GPIO_OUT);
    gpio_set_dir(21, GPIO_OUT);

}