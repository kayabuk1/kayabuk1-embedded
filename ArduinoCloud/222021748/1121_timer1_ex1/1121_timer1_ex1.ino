/*・演習（１）－ timer1_ex1.ino
３桁の7 セグメントLED 表示器を使用します。
電源を入れた直後、7 セグメントLED 表示器に「00.0」を表示、その後100m 秒間隔で
インクリメントします。「59.9」まで表示したら、再度「00.0」からカウントさせます。
組み込みシステムの動作は、以下の通りです。
 ダイナミック点灯時の表示桁切り替えにTimer2 の標準モードを使用する
 計測時間（100ms）には、Timer1 のCTC モードを使用する*/
#include "7seg.h"
#include "7segdisp.h"
/*作りたい時間[s]=(分周比今回は256*(OCRA+1))/16*10^6[Hz] 
  OCRnA = 作りたい時間[s]*(16*10^6[Hz]/分周比256)-1 */
#define PERIOD 0.1
#define CTC_COMP_VALUE PERIOD*(16000000)/256-1 //ﾏｸﾛの入れ子はOKとのこと
#define MAX_COUNT 600   //floatなど小数点はｵｰﾊﾞﾍｯﾄﾞが大きく誤差の元なので極力使わない
volatile int timer_count = 0; //割込みとﾒｲﾝで共有する変数は毎回確認してもらう。
void setup() {
  //---ポート設定---
  DDRB |= 0x0F; 
  DDRD |= 0xF0;
  DDRC |= 0x07; 
  //---Timer1(CTCﾓｰﾄﾞ,100ms間隔)
  cli();  //status registerをクリア＝割り込み禁止に
  bitClear(TCCR1B,WGM13); /* CTC Mode */
  bitSet(TCCR1B,WGM12);   //Timer/Counter Countorl Register 1B,1Aの
  bitClear(TCCR1A,WGM11); //Waveform Generate Modeを0100にすることでCTCモードに
  bitClear(TCCR1A,WGM10); //そういえば、timer/counterになっているのはなぜ？
  bitClear(TCCR1B,CS10); /* プリスケーラ 256 分周 */
  bitClear(TCCR1B,CS11);  //Clock Select 100=256分周期=1clkが1/256clkに
  bitSet(TCCR1B,CS12);
  OCR1A = CTC_COMP_VALUE; /* 比較値設定 *///Output Compare Register 出力比較ﾚｼﾞｽﾀ
                          //TCNT1ﾚｼﾞｽﾀがOCR1Aに入れた値と同じ値になったら信号出力
  bitSet(TIMSK1,OCIE1A); /* コンペアマッチ割込み許可 *///OnClearInterruptEnable?
  TCNT1 = 0;  //Time/Counter Register カウンタ本体
//---Timer2(標準ﾓｰﾄﾞ、ダイナミック点灯用)
bitClear(TCCR2B,WGM22); // 標準モード選択
bitClear(TCCR2A,WGM21);
bitClear(TCCR2A,WGM20);
bitSet(TCCR2B,CS22); // プリスケーラ（256 分周）
bitSet(TCCR2B,CS21);
bitClear(TCCR2B,CS20);
bitSet(TIMSK2,TOIE2); // オーバーフロー割込み許可
TCNT2 = 0;
  sei(); /* （マスクされていない）全割込み許可 *///set status rejister-7bit
}
void loop() {}

ISR(TIMER1_COMPA_vect){
  timer_count++;
  if(timer_count >= MAX_COUNT){
    timer_count = 0;
  }
}
ISR(TIMER2_OVF_vect){ //digの2だけ小数点DPを表示させる様にすること。
  static unsigned char pos = 0x01;  //現在の桁位置(001,010,100)
  int num_to_disp = 0;  //人間が分かる数字で今なにを表示したいか
  unsigned char seg_data = 0 ; //7seg自体に出力するnum_to_dispの形に光らせるbitpattern
//---残像防止の為に一度全消灯
PORTC &= 0xF8; /* 7SEG_LED-全消灯 */
PORTB &= 0xF0; /* 下位4bit クリア */
PORTD &= 0x0F; /* 上位4bit クリア */
//---ISRが呼ばれる度に次の桁に移動(0x01->0x02->0x04->0x01...)
//---1.照らす場所の移動(posの更新)
pos <<=1;
if(pos > 0x04) { pos = 0x01; }
//---今光らせる桁に必要な「1文字だけ」切り出す作業
  switch(pos){   
    case (1 << DIG1): // DIG1=0,左の桁 (10の位) [pos == 0x01]
      // 例: 599 から-> 5を取り出す
      // bitは7...1という順なのに、桁の大きさは0が先でややこしい...なぜだ
      num_to_disp = (timer_count / 100) % 10;
      seg_data = disp[num_to_disp]; //計算した値を索引に7segdisp.hのﾋﾞｯﾄﾊﾟﾀｰﾝ変換数列に
      break; //seg_dataとnum_to_dispが意味が似ていてややこしく感じる
            //どういう役割分けになっているのだろうか

    case (1 << DIG2): // 真ん中の桁 (1の位) + ドット [pos == 0x02]
      // 例: 599 -> 9
      num_to_disp = (timer_count / 10) % 10;
      seg_data = disp[num_to_disp];
      
      // ★ドット(DP)の処理
      // disp配列は0xC0(1100 0000)スタートなので、Active Low(0で点灯)です。
      // 最上位ビット(bit7)を0にするとDPが光ります。
      seg_data &= 0x7F; // 0111 1111 とANDをとってbit7を0にする
                        // 0111 1111 1は上行で計算したそのまま、最上位7bitだけ０に
      break;

    case (1 << DIG3): // 右の桁 (0.1の位) [pos == 0x04]
      // 例: 599 -> 9
      num_to_disp = timer_count % 10;
      seg_data = disp[num_to_disp];
      break;
  }
  //---switch(pos)で計算したseg_dataの形に光らせる
  PORTB |= (seg_data & 0x0F); /* 下位4bit 出力 */
  PORTD |= (seg_data & 0xF0); /* 上位4bit 出力 */
  PORTC |= pos; //最後に桁のスイッチを入れる。
}
