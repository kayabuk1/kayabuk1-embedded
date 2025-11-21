#include "20251029_test_sw.h"

void setup() {
  DDRD |= 0xf0;
  DDRD &= ~((0x1 << SW1)|(0x1 << SW2));
  PORTD &= 0x0F;

}

void loop() {
  sw_state();
  if(sw_flag){
    sw_flag= 0;
    switch(sw){
      case 0:
          break;
      case 1:
          PORTD ^= (0x1 << LED1);
          break;
      case 2:
          PORTD ^= (0x1 << LED2);
          break;
      case 3:
          PORTD ^=(0x1 << LED3);
          PORTD ^=(0x1 << LED4);
          break;
    }
  }

}
