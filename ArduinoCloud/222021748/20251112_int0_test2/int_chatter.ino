#include "int0_chatter.h"

volatile char state = 0;
volatile unsigned long cnt = INT0_CHAT;

ISR(INT0_vect){
    PORTC ^= (0x1 << PC3); // LED の反転
    bitClear(EIMSK,INT0); // INT0 割込み禁止
    state = 1; // チャタリング対策開始
    cnt = INT0_CHAT;  //cnt = INT0_CHAT = 10000UL
}
void sw_state(void)
{
    switch(state){
      case 1:
      --cnt;
      if(cnt == 0){
        state = 0;
        cnt = INT0_CHAT;
      }break;
      case 0:
      if(bitRead(PIND,PD2)){
      --cnt;
        if(cnt == 0){
          bitSet(EIFR,INTF0);
          bitSet(EIMSK,INT0);
          cnt = INT0_CHAT;
        }
      }
    }
}