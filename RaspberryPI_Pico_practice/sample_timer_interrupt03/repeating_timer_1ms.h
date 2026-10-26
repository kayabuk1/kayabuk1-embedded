#ifndef REPEATING_TIMER_1MS_H
#define REPEATING_TIMER_1MS_H

#include "pico/stdlib.h"   // bool と struct repeating_timer のため

// 状態の定義（ステートマシーンを作る為の下準備）
typedef enum 
{
    STATE_1 = 1, // = 0; と書かなくてもデフォで０から割当してくれる。
    STATE_2,
    STATE_3,
} state_t;

extern volatile state_t state;

bool repeating_timer_callback(
    __unused struct repeating_timer *t);

#endif