#include "20251029_test_sw.h"
#define COUNT 10000UL

unsigned char sw = 0;
unsigned char sw_flag;

void sw_state(){
  static char state = 0;
  static unsigned long cnt = 0;
  unsigned char tmp;
  
  tmp = (~PIND >> 2)& 0x3;

  switch(state){
    case 0:
        if(sw != tmp){
          cnt = COUNT;
          state = 1;
        }
        break;
    case 1:
        cnt--;
        if(cnt==0){
          if(sw!=tmp){
            sw_flag=1;
            sw=tmp;
          }
          state = 0;
        }
        break;
  }
}