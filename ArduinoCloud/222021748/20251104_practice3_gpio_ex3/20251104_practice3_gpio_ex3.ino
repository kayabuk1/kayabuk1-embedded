#define LED1 4
#define LED2 5
#define LED3 6
#define LED4 7
#define SW1 2
#define SW2 3
#define COUNT 10000UL
unsigned char sw = 0;
unsigned char sw_flag;
unsigned char ledCount = 0;

void sw_state(){
    static char state =0;
    static unsigned long cnt =0;
    unsigned char tmp;
    tmp = (~PIND >> 2)&0x3;
//PINDからPD2とPD3のﾋﾞｯﾄを取り出す操作
//PINDはﾎﾟｰﾄDの全8ﾋﾟﾝの状態を読み取る
//SWは押したらLOWを押したらHIGHという人に理解易変換
//PIND全体を2ﾋﾞｯﾄ右にずらす0000 1100→0000 0011
//0x3つまり0000 0011との論理積を取るので、
//0番目のﾋﾞｯﾄと1番目のﾋﾞｯﾄ以外はすべて強制的に0になる
//tmp =0b0000 0000 = case 0 SW1,2もOFF
//tmp =0b0000 0001 = case 1 sw1のみON
//tmp =0b0000 0010 = case 2 sw2のみON
//tmp =0b0000 0011 = case 3 sw1,2もON
    switch(state){
      case 0:
        if(sw != tmp){ //SWを押すとtmpが変わり
          cnt = COUNT;
          state =1;
        }
        break;
      case 1:
        cnt--;
        if(cnt==0){
          if(sw !=tmp){
            sw_flag =1;
            sw = tmp;
          }
          state =0;
        }
        break;
    }
}

void setup() {
  DDRD |= 0xf0; //下位ﾋﾞｯﾄは初期化されていない
  DDRD &= ~((0x1 << SW1)|(0x1 << SW2));
  //0000 0100|0000 1000=0000 0110 ~ 1111 0011
  //1111 0000 &= 1111 0011 = 1111 0000
  //なので、この操作は必要
  PORTD &= 0x0F;
}

void loop() {
  sw_state();
  if(sw_flag){
      sw_flag = 0;
      switch()
  }

}
