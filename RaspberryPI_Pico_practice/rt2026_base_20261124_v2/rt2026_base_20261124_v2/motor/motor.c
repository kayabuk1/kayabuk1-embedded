/**
 * @file  motor.c
 * @brief モータードライバ TB67H450FNG（左右2個）を動かします。説明は motor.h にあります。
 */
#include "motor.h"

#include "board.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

static uint slice_l;
static uint slice_r;
static uint16_t pwm_top; /* カウンタの上限。レベルを pwm_top + 1 にすると、ずっとHになる */

void motor_init(void)
{
    /* 例：150MHz ÷ 200kHz = 750。カウンタは 0〜749 を数えるので、上限は 749 です。 */
    pwm_top = (uint16_t)(clock_get_hz(clk_sys) / MOTOR_PWM_HZ - 1u);

    const uint pins[4] = {PIN_MOTOR_L_IN1, PIN_MOTOR_L_IN2, PIN_MOTOR_R_IN1, PIN_MOTOR_R_IN2};
    for (int i = 0; i < 4; i++) {
        gpio_set_function(pins[i], GPIO_FUNC_PWM);
    }
    /* GP18とGP19は同じスライス（1番）のAとB、GP20とGP21はスライス2番のAとBです。 */
    slice_l = pwm_gpio_to_slice_num(PIN_MOTOR_L_IN1);
    slice_r = pwm_gpio_to_slice_num(PIN_MOTOR_R_IN1);

    const uint slices[2] = {slice_l, slice_r};
    for (int i = 0; i < 2; i++) {
        pwm_set_clkdiv(slices[i], 1.0f);
        pwm_set_wrap(slices[i], pwm_top);
        pwm_set_both_levels(slices[i], 0, 0); /* IN1 = L、IN2 = L（惰性） */
        pwm_set_enabled(slices[i], true);
    }
}

/** 1個のモーターに duty を設定します。A が IN1、B が IN2 です。 */
static void set_one(uint slice, float duty)
{
    if (duty > 1.0f) {
        duty = 1.0f;
    }
    if (duty < -1.0f) {
        duty = -1.0f;
    }
    const uint16_t full = (uint16_t)(pwm_top + 1u); /* ずっとH */
    if (duty >= 0.0f) {
        /* IN1 = H、IN2 = Hの割合が (1 - duty)。duty = 0 なら両方Hで、ブレーキになります。 */
        pwm_set_both_levels(slice, full, (uint16_t)((1.0f - duty) * (float)full));
    } else {
        /* IN1 = Hの割合が (1 - |duty|)、IN2 = H。duty は負なので、1 + duty が 1 - |duty| です。 */
        pwm_set_both_levels(slice, (uint16_t)((1.0f + duty) * (float)full), full);
    }
}

void motor_set(float duty_l, float duty_r)
{
    set_one(slice_l, (float)MOTOR_L_SIGN * duty_l);
    set_one(slice_r, (float)MOTOR_R_SIGN * duty_r);
}

void motor_brake(void)
{
    motor_set(0.0f, 0.0f);
}

void motor_free(void)
{
    pwm_set_both_levels(slice_l, 0, 0);
    pwm_set_both_levels(slice_r, 0, 0);
}
