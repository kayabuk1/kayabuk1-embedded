/**
 * @file  sw.h
 * @brief タクトスイッチ2個の「短く押した」「長押しした」を見分けます。
 */
#ifndef SW_H
#define SW_H

#include <stdbool.h>

#define SW_NUM         2
#define SW_DEBOUNCE_MS 20   /* この時間、同じ値が続いたら、押した／離したと決める [ms] */
#define SW0_LONG_MS    1000 /* スイッチ0の長押しと見なす時間 [ms]                      */
#define SW1_LONG_MS    2000 /* スイッチ1の長押しと見なす時間 [ms]                      */

typedef enum {
    SW_EV_NONE = 0, /* 何も起きていない                                     */
    SW_EV_SHORT,    /* 短く押して、離した                                   */
    SW_EV_LONG,     /* 長押しの時間に達した（押している間に1回だけ出ます）  */
} sw_event_t;

/** ピンを設定します。起動時に1回呼びます。 */
void sw_init(void);

/** スイッチの状態を更新します。1msの割り込みから呼びます。 */
void sw_update_1ms(void);

/** たまっている出来事を1つ取り出します（取り出すと消えます）。id は 0 か 1 です。 */
sw_event_t sw_take_event(int id);

/** 今、押されていれば true。id は 0 か 1 です。 */
bool sw_is_pressed(int id);

/**
 * 今押されているスイッチについて、「この押し方では、短押しも長押しも出さない」ようにします。
 * 1msの割り込みから呼びます。走行を止めるために押したスイッチが、離したときに
 * 「短押し＝走行開始」として働いてしまうのを防ぐために使います。
 */
void sw_ignore_current_press(void);

/* 先生のサンプル（rt2026）と同じ名前でも読めるようにしてあります。押されていれば true です。 */
static inline bool getTactSW0(void) { return sw_is_pressed(0); }
static inline bool getTactSW1(void) { return sw_is_pressed(1); }

#endif /* SW_H */
