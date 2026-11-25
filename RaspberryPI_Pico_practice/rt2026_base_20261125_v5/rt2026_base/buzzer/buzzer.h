#ifndef BUZZER_H
#define BUZZER_H
#include <stdio.h>
#include "pico/stdlib.h"

#include "board.h"
#define BUZZER_PIN PIN_BUZZER /* 2026年モデルの基板では GP22（PWM3A） */
/*======================================*/
/* 定数			                        */
/*======================================*/
typedef enum {
	DO_6 = 1047,
	RE_6 = 1175,
	MI_6 = 1319,
	FA_6 = 1397,
	SO_6 = 1568,
	RA_6 = 1760,
	SI_6 = 1976,
	DO_7 = 2094,
} scaleSelect;

void setBeepPattern(uint pin, uint16_t data, scaleSelect freq);
void initBeep(void);
void beepProcess(void);

#endif /* BUZZER_H */
