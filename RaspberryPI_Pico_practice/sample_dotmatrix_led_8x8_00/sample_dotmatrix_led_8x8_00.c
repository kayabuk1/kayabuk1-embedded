#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/timer.h"

#include "buzzer/buzzer.h"
#include "pico_hardware_setup/pico_hardware_setup.h"
#include "repeating_timer1ms.h"
#include "dotmatrix_led_8x8/dotmatrix_led_8x8.h"

#include "quadrature_encoder.pio.h"

PIO pio = pio0;
const uint sm = 0;

extern int32_t encoder_value, encoder_past;

int main() {
    const uint PIN_AB = 16;
    struct repeating_timer timer;

    hardware_setup();
    initBeep();
    init_spi();
    stdio_init_all();

    add_repeating_timer_ms
    (-1, repeating_timer_callback, NULL, &timer);

    printf("Hello from quadrature encoder\n");

    pio_add_program(pio, &quadrature_encoder_program);
    // PIO(Programmable I/O)の命令メモリに命令をロードする為の関数。

    quadrature_encoder_program_init(pio, sm, PIN_AB, 0);
    // PIO（Programmable I/O）を使用したロータリーエンコーダ
    // （Quadrature Encoder）用プログラムの初期化関数。
    

    while (1) {
        printf("position %8d\n", encoder_value);
        sleep_ms(100);
    }
}

