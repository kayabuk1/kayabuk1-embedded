#include "int0_chatter.h"

void setup() {
bitSet(DDRC,PC3); // PC3-OUTPUT
bitSet(PORTD,PD2); // 内部プルアップ有効
bitClear(EICRA,ISC00); // ISC00=0
bitSet(EICRA,ISC01); // ISC01=1
bitSet(EIMSK,INT0); // INT0-ENABLE
bitClear(PORTC,PC3); // LED-消灯
}
void loop(void) {
sw_state();
}