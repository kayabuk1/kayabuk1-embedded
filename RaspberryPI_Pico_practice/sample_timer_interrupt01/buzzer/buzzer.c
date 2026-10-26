#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "buzzer.h"

/*======================================
/ 定数設定
/======================================*/
#define BEEP_ON 1
#define BEEP_OFF 0
#define BEEP_ONCE_TIME 50       // ビットパターン1bitの鳴動時間[ms]
#define BEEP_SET_PATTERN_NUM 16 // ビープ音パターン設定個数
/*======================================
/ プロトタイプ宣言（ローカル）
/======================================*/
void beepOut(char bp);

/*====================================
/ グローバル変数宣言
/======================================*/
static uint32_t beep_timer; /* ブザー用タイマ               */
static uint16_t beep_pattern;    /* ブザーパターン               */
static int beep_mode;            /* ブザーの処理状態             */
static uint slice;

/************************************************************************
/ モジュール名 initBeep
/ 処理概要     ブザー関連初期化
/ 引数         なし
/ 戻り値       なし
/************************************************************************/
void initBeep(void)
{
    beep_timer = 0;
    beep_pattern = 0x0000;
    beep_mode = 0;
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
}

/***********************************************************************
/ モジュール名  setBeepPattern
/ 処理概要      ブザー出力パターンセット
/ 引数      uint16_t パターン
/ 戻り値   	なし
/************************************************************************/
void setBeepPattern(uint pin, uint16_t data, scaleSelect freq)
{
    beep_timer = 0;
    beep_pattern = data;
    beep_mode = 1;

    // スライスNoを取得
    slice = pwm_gpio_to_slice_num(pin);

    // クロック周波数取得
    uint32_t clock = clock_get_hz(clk_sys);

    float divider = 4.0f; // 分周
    pwm_set_clkdiv(slice, divider);

    uint32_t wrap = (clock / divider / freq) - 1;

    // 1周期のwrap値を設定
    pwm_set_wrap(slice, wrap);
    // duty50%
    pwm_set_chan_level(slice, pwm_gpio_to_channel(pin), wrap / 2);
}

/************************************************************************
/ モジュール名	ブザー処理
/ 処理概要		この関数を1ms毎に実行することで
/ 				setBeepPattern()で設定したパターンでブザー出力
/ 引数     	なし
/ 戻り値   	なし
/************************************************************************/
void beepProcess(void)
{
    if (beep_mode == 1)
    {
        if (beep_timer % BEEP_ONCE_TIME == 0)
        {
            if (beep_pattern & 0x8000)
            {
                beepOut(BEEP_ON);
            }
            else
            {
                beepOut(BEEP_OFF);
            }
            beep_pattern <<= 1;
        }
        beep_timer++;
        if (beep_timer > BEEP_ONCE_TIME * BEEP_SET_PATTERN_NUM)
        {
            beepOut(BEEP_OFF);
            beep_mode = 0;
        }
    }
}

/************************************************************************
/ モジュール名	beepOut
/ 処理概要		ビープ音鳴動
/ 引数     	0：OFF，0以外：ON
/ 戻り値   	なし
/************************************************************************/
void beepOut(char bp)
{
    if (bp == 0)
    {
        pwm_set_enabled(slice, false);
        // PWMをOFFにしたとき、GPIOピンがHの場合あり
        // FETがONのままになり、ブザーに電流が流れ続けるため（発熱した！！）
        // 強制的にGPIOモード（GPIO_FUNC_SIO）にして、LOWへ変更
        gpio_set_function(BUZZER_PIN, GPIO_FUNC_SIO);
        gpio_put(BUZZER_PIN, 0);
    }
    else
    {
        // PWMをセットする場合は、上記の対応をしているため、PWMモードにしている
        gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
        pwm_set_enabled(slice, true);
    }
}
