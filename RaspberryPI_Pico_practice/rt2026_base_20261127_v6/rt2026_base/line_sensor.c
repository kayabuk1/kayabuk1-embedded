/**
 * @file  line_sensor.c
 * @brief ラインセンサーとマーカーセンサーを読みます。説明は line_sensor.h にあります。
 */
#include "line_sensor.h"

#include "adc_mcp3208or3008/adc_mcp3208or3008.h"
#include "board.h"
#include "params.h"
#include "pico/stdlib.h"

/* キャリブレーションをする前の値です。仕様書 表3-1（10ビットで、白 530、黒 998）を、
 * 使っているICのビット数に合わせて換算しています。
 */
#define DEFAULT_WHITE ((uint16_t)(530u * MCP3208or3008_RESOLUTION / 1024u))
#define DEFAULT_BLACK ((uint16_t)(998u * MCP3208or3008_RESOLUTION / 1024u))

/* ラインセンサー6個の重み。sensor[0]（右端）から sensor[5]（左端）の順です（仕様書 表3-2）。 */
static const int8_t weight[LINE_SENSOR_NUM] = {-5, -3, -1, +1, +3, +5};

/* sensor[i] がつながっている、A/D変換ICのチャンネルの番号（board.h で決めます）。 */
static const uint8_t adc_ch[SENSOR_NUM] = SENSOR_ADC_CH;

static uint16_t raw[SENSOR_NUM];
static uint8_t  norm[SENSOR_NUM];
static bool     white[SENSOR_NUM];
static uint16_t cal_min[SENSOR_NUM];
static uint16_t cal_max[SENSOR_NUM];
static uint16_t keep_min[SENSOR_NUM]; /* キャリブレーションに失敗したときに戻す値 */
static uint16_t keep_max[SENSOR_NUM];
static bool     cal_ok_once = false;
static uint8_t  cal_fail_bits = 0;
static float    displacement = 0.0f;
static bool     lost = false;

void line_sensor_init(void)
{
    mcp3208or3008_init();

    gpio_init(PIN_LINE_LED);
    gpio_set_dir(PIN_LINE_LED, GPIO_OUT);
    gpio_put(PIN_LINE_LED, !LINE_LED_ON_LEVEL); /* 消しておく */
    /* ゲートドライバー（MCP1402）を外し、Picoのピンで直接LEDのスイッチを切り替えることになったので、
     * このピンの出力を、いちばん強い設定（12mA）にしておきます。切り替わりが速くなります。
     */
    gpio_set_drive_strength(PIN_LINE_LED, GPIO_DRIVE_STRENGTH_12MA);

    for (int i = 0; i < SENSOR_NUM; i++) {
        cal_min[i] = DEFAULT_WHITE;
        cal_max[i] = DEFAULT_BLACK;
    }
}

void line_sensor_sample(void)
{
    /* LEDを点け、明るさが落ち着くのを30µs待ってから読みます（先生のサンプルと同じ時間）。 */
    gpio_put(PIN_LINE_LED, LINE_LED_ON_LEVEL);
    busy_wait_us_32(30);
    for (int i = 0; i < SENSOR_NUM; i++) {
        raw[i] = mcp3208or3008_read(adc_ch[i]);
    }
    gpio_put(PIN_LINE_LED, !LINE_LED_ON_LEVEL);

    int32_t sum = 0;
    int32_t line_max = 0; /* ラインセンサー6個の、正規化した値の最大 */
    for (int i = 0; i < SENSOR_NUM; i++) {
        /* 正規化（仕様書 3.1.3）：(最大 - 今の値) ÷ (最大 - 最小) × 100。
         * A/D変換の値は白いほど小さいので、白が100、黒が0になります。
         */
        const int32_t span = (int32_t)cal_max[i] - (int32_t)cal_min[i];
        int32_t n = 0;
        if (span > 0) {
            n = ((int32_t)cal_max[i] - (int32_t)raw[i]) * 100 / span;
        }
        if (n < 0) {
            n = 0;
        }
        if (n > 100) {
            n = 100;
        }
        norm[i] = (uint8_t)n;

        /* 2値化（仕様書 3.1.4）。境目で白と黒がばたつかないように、幅を持たせています。 */
        if (n > WHITE_ON) {
            white[i] = true;
        } else if (n < WHITE_OFF) {
            white[i] = false;
        }

        if (i < LINE_SENSOR_NUM) {
            sum += (int32_t)weight[i] * n;
            if (n > line_max) {
                line_max = n;
            }
        }
    }

    /* 6個とも、白がほとんど見えていない（ラインを見失った）ときは、ずれを更新しません。
     * 直前のずれを持ち続けるので、ラインがあった側へ曲がり続けます。
     * 2値の結果ではなく、正規化した値で判断しています。ラインが2個のセンサーの間に
     * あるときは、どちらも「白とも黒とも言えない値」になることがあるためです。
     */
    lost = (line_max < LOST_NORM);
    if (!lost) {
        float d = (float)sum * DISP_SCALE * (float)LINE_DISP_SIGN;
        if (d > 100.0f) {
            d = 100.0f;
        }
        if (d < -100.0f) {
            d = -100.0f;
        }
        displacement = d;
    }
}

uint16_t line_raw(int i)      { return raw[i]; }
uint8_t  line_norm(int i)     { return norm[i]; }
bool     line_is_white(int i) { return white[i]; }

uint8_t line_bits(void)
{
    uint8_t b = 0;
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (white[i]) {
            b |= (uint8_t)(1u << i);
        }
    }
    return b;
}

int line_white_count(void)
{
    int c = 0;
    for (int i = 0; i < LINE_SENSOR_NUM; i++) {
        if (white[i]) {
            c++;
        }
    }
    return c;
}

bool  line_is_lost(void)      { return lost; }
float line_displacement(void) { return displacement; }

void line_cal_begin(void)
{
    for (int i = 0; i < SENSOR_NUM; i++) {
        keep_min[i] = cal_min[i];
        keep_max[i] = cal_max[i];
        cal_min[i] = 0xFFFF; /* どんな値よりも大きい値から始めて、小さい値が来るたびに更新する */
        cal_max[i] = 0;
    }
}

void line_cal_update(void)
{
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (raw[i] < cal_min[i]) {
            cal_min[i] = raw[i];
        }
        if (raw[i] > cal_max[i]) {
            cal_max[i] = raw[i];
        }
    }
}

bool line_cal_end(void)
{
    bool ok = true;
    cal_fail_bits = 0;
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (cal_max[i] < cal_min[i] || (cal_max[i] - cal_min[i]) < CAL_MIN_SPAN) {
            ok = false; /* このセンサーは、白と黒の両方を見ていない */
            cal_fail_bits |= (uint8_t)(1u << i);
        }
    }
    if (ok) {
        cal_ok_once = true;
    } else {
        for (int i = 0; i < SENSOR_NUM; i++) {
            cal_min[i] = keep_min[i];
            cal_max[i] = keep_max[i];
        }
    }
    return ok;
}

bool     line_cal_done(void)  { return cal_ok_once; }
uint8_t  line_cal_fail_bits(void) { return cal_fail_bits; }
uint16_t line_cal_min(int i)  { return cal_min[i]; }
uint16_t line_cal_max(int i)  { return cal_max[i]; }
