#include "7seg.h"
#include "sw.h"
void delayMs(volatile unsigned long time)
{
  time *= 2000UL;
  while(time--);
}
void setup(){
  DDRB |= 0x0F;
  DDRD |= 0xF0;
  DDRC |= 0x07;
  PORTD |= ((0x1 << SW1) | (0x1 << SW2));
  bitSet(PORTC,DIG1);   //ｷｬﾒﾙｹｰｽ、関数2語以降の単語を大文字にする習慣
                        //全部大文字にするのは変わらない常数を示す
  disp7(0);
}
void loop()
{
  static int num = 0;//数を数える変数
  static char x = 0;  //1の桁
  static char y = 0;  //10の桁
  static char z = 0;  //100の桁
  sw_state();
    if(sw_flag)
    {
      sw_flag = 0; //旗が立っているのを見たらすぐ旗を降ろす。
      switch(sw)
      {
        case 0:
          break;
        case 1: // SW1-ON //ここにSW1の＋１カウントする処理
          num++;
          if(num > 999){
            num = 0;
          }
          break;
        case 2: // SW2-ON //ここにSW2を押すと桁が移動する処理
          if(num == 0){
            num = 999;
          }else{
            num--;
          }
          break;
        case 3: // SW1-ON,SW2-ON
          break;
      }
      x = num %10;
      y = (num/10)%10;
      z = num /100;
    }
    dinamic_dis7(0x1,z);
    delayMs(5);
    dinamic_dis7(0x2,y);
    delayMs(5);
    dinamic_dis7(0x4,x);
    delayMs(5);
}