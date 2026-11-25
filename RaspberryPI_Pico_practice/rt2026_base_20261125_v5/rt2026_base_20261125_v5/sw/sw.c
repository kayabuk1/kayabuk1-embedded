/**
 * @file  sw.c
 * @brief タクトスイッチ2個の「短く押した」「長押しした」を見分けます。説明は sw.h にあります。
 */
#include "sw.h"

#include "board.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

typedef struct {
    uint     pin;
    uint16_t long_ms;    /* 長押しと見なす時間                                             */
    bool     pressed;    /* チャタリングを取り除いた後の状態                               */
    uint8_t  change_ms;  /* 今の pressed と違う値が、続けて読めた時間                      */
    uint16_t release_ms; /* 離れた状態が続いた時間（電源を入れた直後の確認に使う）         */
    bool     armed;      /* 一度「離れている」ことを確かめたら true。それまでは何も出さない */
    uint16_t held_ms;    /* 押し続けている時間                                             */
    bool     long_fired; /* この押し方で、長押しをもう出したか                             */
    volatile sw_event_t event;
} sw_t;

static sw_t sw[SW_NUM] = {
    {.pin = PIN_SW0, .long_ms = SW0_LONG_MS},
    {.pin = PIN_SW1, .long_ms = SW1_LONG_MS},
};

void sw_init(void)
{
    for (int i = 0; i < SW_NUM; i++) {
        gpio_init(sw[i].pin);
        gpio_set_dir(sw[i].pin, GPIO_IN);
        /* 押すとLOWになる配線ならプルアップ、押すとHIGHになる配線ならプルダウンにします。
         * RP2350 には、「マイコンの中のプルダウンだけでは、ピンをLOWに保てないことがある」という
         * 不具合の報告（RP2350-E9）があります。押すとHIGHになる配線にするときは、
         * 基板に外付けのプルダウン抵抗が要ります。
         */
        if (SW_PRESSED_LEVEL == 0) {
            gpio_pull_up(sw[i].pin);
        } else {
            gpio_pull_down(sw[i].pin);
        }
    }
}

void sw_update_1ms(void)
{
    for (int i = 0; i < SW_NUM; i++) {
        sw_t *s = &sw[i];
        const bool raw = (gpio_get(s->pin) == (SW_PRESSED_LEVEL != 0));

        /* チャタリングの除去：今の状態と違う値が SW_DEBOUNCE_MS 続いたら、状態を変えます。 */
        if (raw != s->pressed) {
            if (++s->change_ms >= SW_DEBOUNCE_MS) {
                s->pressed = raw;
                s->change_ms = 0;
            }
        } else {
            s->change_ms = 0;
        }

        /* 電源を入れた直後は、離れた状態が100ms続くのを確かめるまで、何も出しません。
         * board.h の SW_PRESSED_LEVEL が実機と逆だった場合に、「押しっぱなし」と読めて、
         * 長押しの動き（全消去）が勝手に始まるのを防ぐためです。
         */
        if (!raw) {
            if (s->release_ms < 1000) {
                s->release_ms++;
            }
        } else {
            s->release_ms = 0;
        }
        if (!s->armed) {
            if (s->release_ms >= 100) {
                s->armed = true;
            }
            continue;
        }

        if (s->pressed) {
            if (s->held_ms < 60000) {
                s->held_ms++;
            }
            if (!s->long_fired && s->held_ms >= s->long_ms) {
                s->long_fired = true;
                s->event = SW_EV_LONG;
            }
        } else {
            if (s->held_ms > 0 && !s->long_fired) {
                s->event = SW_EV_SHORT; /* 長押しに達する前に離した */
            }
            s->held_ms = 0;
            s->long_fired = false;
        }
    }
}

sw_event_t sw_take_event(int id)
{
    if (id < 0 || id >= SW_NUM) {
        return SW_EV_NONE;
    }
    if (sw[id].event == SW_EV_NONE) {
        return SW_EV_NONE; /* 何も起きていないときは、割り込みを止めずに戻る */
    }
    /* 読んでから消すまでの間に、割り込みが新しい出来事を書くと、それを消してしまいます。
     * 割り込みを止めている間に、読んで、消します。
     */
    const uint32_t ints = save_and_disable_interrupts();
    const sw_event_t e = sw[id].event;
    sw[id].event = SW_EV_NONE;
    restore_interrupts(ints);
    return e;
}

bool sw_is_pressed(int id)
{
    if (id < 0 || id >= SW_NUM) {
        return false;
    }
    return sw[id].armed && sw[id].pressed;
}

void sw_ignore_current_press(void)
{
    for (int i = 0; i < SW_NUM; i++) {
        if (sw[i].pressed) {
            /* 「長押しをもう出した」ことにします。こうすると、押し続けても長押しは出ず、
             * 離したときの短押しも出ません（sw_update_1ms の、離したときの処理を参照）。
             */
            sw[i].long_fired = true;
            sw[i].event = SW_EV_NONE;
        }
    }
}
