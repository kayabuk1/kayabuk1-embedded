#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/spi.h"
#include "pico/binary_info.h"
#include "hardware/pwm.h"

#include "rt2026_hwsetup.h"
#include "rt2026_digital_io.h"
#include "rt2026_spi.h"
#include "rt2026_buzzer.h"
#include "rt2026_repeating_timer.h"
#include "rt2026_pwm.h"
#include "rt2026_encoder.h"
#include "rt2026_flush.h"
#include "rt2026_uart.h"


#include "quadrature_encoder.pio.h"

uint slice; // ブザー用、今後ローカル変数化する
extern int uart_buffer[];
// MPU9250出力データ格納用
int16_t gyro[3];
// ラインセンサ、マーカセンサ格納用
int16_t line_marker[8];

// デバッグしたらインターバルタイマ内のローカル変数にする
extern float displacementForLine;

// PIOからの出力を受け取る変数、インターバルタイマ内に移動する
int left_enc_count, right_enc_count;

//
extern odom_enc_t enc;

// ログ状態
char log_condition = 0;

/* main program */
int main()
{

    // Base pin to connect the A phase of the encoder.
    // The B phase must be connected to the next pin
    const uint PIN_AB = 10;
    const uint PIN_CD = 12; // モータ2つ目

    stdio_init_all();
    HardwareSetup();

    PIO pio = pio0;
    PIO pio_second = pio1; // モータ2つ目、設定が怪しい

    const uint sm = 0;
    const uint sm2 = 0;

    // we don't really need to keep the offset, as this program must be loaded
    // at offset 0
    pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, PIN_AB, 0);

    pio_add_program(pio_second, &quadrature_encoder2_program);
    quadrature_encoder_program_init(pio_second, sm2, PIN_CD, 0);

    // This example will use SPI0 at 1MHz.
    spi_init(SPI_PORT, 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Make the SPI pins available to picotool
    bi_decl(bi_3pins_with_func(PIN_MISO, PIN_MOSI, PIN_SCK, GPIO_FUNC_SPI));
    // Make the CS pin available to picotool
    // bi_decl(bi_1pin_with_name(PIN_CS0, "SPI CS"));

    mpu9250_reset(PIN_CS0);

    // Create a repeating timer that calls repeating_timer_callback.
    // If the delay is > 0 then this is the delay between the previous callback ending and the next starting.
    // If the delay is negative (see below) then the next call to the callback will be exactly 500ms after the
    // start of the call to the last callback
    struct repeating_timer timer;
    add_repeating_timer_ms(-2, repeating_timer_callback, NULL, &timer);

    // pwm initialize
    motor_driver_init();

    // buzzer
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    initBeep();
    // pwm_set_frequency(BUZZER_PIN, C6);
    // pwm_set_enabled(slice, true);

    // ログ取得イニシャライズ
    log_init();
    log_condition = 1; // フラッシュメモリ準備完了

    //uartイニシャライズ
    uart_init_115200();

    while (1)
    {
        left_enc_count = quadrature_encoder_get_count(pio, sm);
        // モータ2つ目(カウンタの符号を逆にする)
        right_enc_count = -quadrature_encoder2_get_count(pio_second, sm2);

        // busy_wait_us(100 * 1000);

        // ログ関係処理
        log_task();
        if (log_condition == 99)
        {
            cancel_repeating_timer(&timer);
            log_stop();
            break;
        }
    }

    while (1)
    {
        if (getTactSW0())
        {
            log_dump_binary();
            break;
        }
    }
}
