#include "7seg.h"
#define COUNT 20UL

unsigned char sw = 0; // SW-ON:1,SW-OFF:0 とする
unsigned char sw_flag;

void sw_state(){
static char state = 0;        //最初だけ0で初期化。stateの寿命終りまで
static unsigned long cnt = 0; //カウント用変数cntも同様に
unsigned char tmp;

tmp = (~PIND >> 2) & 0x3; // SW の状態を取得
/*
PINDの下位2ビット,2,1番がSWに割振られている。そしてSWは押すとLow
例SW2=3だけ押されている場合、0b1111 0111
例SW1=2だけ押されている場合  0b1111 1011 3,2,1,0bitなので数えと1つズレ
演算子としては反転が先。演算子はまずその数を確定させて他の数との演算に
使えるようにするように優先順位が付けられていると言う理解でよい？
0b0000 0100 人の直観に即した押されたらHighになる状態にする。
2bit右ｼﾌﾄ　0b0000 0001 
  0000 0001
& 0000 0011　なので上位6bitが強制的に0にクリアされ、
取得した値をarduinoでは符号付整数最大8ビットのchar型変数tmpに代入。
なのでﾋﾞｯﾄ列状態ではなく00=0, 01=1, 10=2として扱われる？
そしてその値でswitch中ifで判定式に使う。
*/
switch(state)
{ case 0: if(sw != tmp){cnt = COUNT; state = 1;} break;
  case 1: cnt--; if (cnt == 0){if(sw != tmp){sw_flag = 1;sw = tmp;}
         state = 0;}break;}
}
/*もしsw2押されていたら、最初state=0,case0,if,sw=0,tmp=2,cnt=10000,
state=1,break;ﾁｬﾀﾘﾝｸﾞ防止の為にcnt=10000,notif,loop,state=1,case1
cnt--,×10000回,cnt==0,if,sw=0,tmp=2,sw_flag=1,sw=tmp=2,state=0,break;
loop()内でここで立てたsw_flagとswを判定式に利用する。
*/