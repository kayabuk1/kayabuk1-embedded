#include "20251104_7seg.h"

void delayMs(volatile unsigned long time)
{
  time *= 2000UL;
  while(time--);
}
void setup(){
  DDRB |= 0x0F;
  DDRD |= 0xF0;
  DDRC |= 0x07;
  bitSet(PORTC,DIG1);   //ｷｬﾒﾙｹｰｽ、関数2語以降の単語を大文字にする習慣
                        //全部大文字にするのは変わらない常数を示す
  disp7(0);
}
void loop(){
  //static char num = 0;  //静的ﾛｰｶﾙ変数初期化は最初だけ。寿命は終わるまで
  //delayMs(500);
  //if(++num >= 10){
  //  num=0;
  //}
  //disp7(num);
  dinamic_dis7(0x1,1);
  delayMs(1);
  dinamic_dis7(0x2,2);
  delayMs(1);
  dinamic_dis7(0x4,3);
  delayMs(1);
}