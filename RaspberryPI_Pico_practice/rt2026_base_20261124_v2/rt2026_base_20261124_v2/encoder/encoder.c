/**
 * @file  encoder.c
 * @brief 左右のエンコーダを読みます。【仮のドライバ】 説明は encoder.h にあります。
 */
#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "encoder.h"
#include "board.h"
#include "quadrature_encoder.pio.h" /* ビルドのときに quadrature_encoder.pio から作られます */

#define ENC_PIO  pio0
#define ENC_SM_L 0u /* 左：GP6（A相）、GP7（B相） */
#define ENC_SM_R 1u /* 右：GP8（A相）、GP9（B相） */

/* 数1個あたりに進む距離 [m]。タイヤ1周 = 3.14159 × 0.080m = 0.2513m、1周で840個。 */
#define M_PER_COUNT (3.14159265f * TIRE_DIAMETER_M / (float)ENC_PULSE_PER_REV)

static int32_t count_l;
static int32_t count_r;
static int32_t hist_l[ENC_SPEED_WINDOW_MS]; /* ENC_SPEED_WINDOW_MS 回分の、過去の数 */
static int32_t hist_r[ENC_SPEED_WINDOW_MS];
static uint32_t hist_pos;
static float speed_l;
static float speed_r;

void encoder_init(void)
{
    /* PIOのプログラムは、0番地に置く決まりです（.pio の中で .origin 0 と指定されています）。
     * 同じプログラムを、2つのステートマシン（左用と右用）で使います。
     */
    pio_add_program(ENC_PIO, &quadrature_encoder_program);
    quadrature_encoder_program_init(ENC_PIO, ENC_SM_L, PIN_ENC_L_A, 0);
    quadrature_encoder_program_init(ENC_PIO, ENC_SM_R, PIN_ENC_R_A, 0);
}

void encoder_update_1ms(void)
{
    count_l = ENC_L_SIGN * quadrature_encoder_get_count(ENC_PIO, ENC_SM_L);
    count_r = ENC_R_SIGN * quadrature_encoder_get_count(ENC_PIO, ENC_SM_R);

    /* hist_x[hist_pos] には、ENC_SPEED_WINDOW_MS 回前の数が入っています。
     * 今の数との差が、その間に進んだ数です。距離に直して、時間で割ると速さになります。
     */
    const float window_s = (float)ENC_SPEED_WINDOW_MS * 0.001f;
    speed_l = (float)(count_l - hist_l[hist_pos]) * M_PER_COUNT / window_s;
    speed_r = (float)(count_r - hist_r[hist_pos]) * M_PER_COUNT / window_s;
    hist_l[hist_pos] = count_l;
    hist_r[hist_pos] = count_r;
    hist_pos = (hist_pos + 1u) % ENC_SPEED_WINDOW_MS;
}

int32_t encoder_count(bool left)
{
    return left ? count_l : count_r;
}

float encoder_speed_mps(bool left)
{
    return left ? speed_l : speed_r;
}

float encoder_distance_m(void)
{
    return (float)(count_l + count_r) * 0.5f * M_PER_COUNT;
}
