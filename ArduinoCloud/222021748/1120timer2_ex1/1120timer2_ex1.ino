#define OVF_COUNT 49  //200ms/4.049ms=約49回
void setup() 
{
  bitset(DDRB,0x0f); //7seg点灯に使うﾎﾟｰﾄを出力方向１に設定
  bitset(DDRD,0xf0);
  bitset(DDRC,0x0f);
  cil();                  //SREGの7bit目、全割込許可bitを0禁止に
                          //その間に割込みの設定を行う。
  bitclear(TCCR2B,WGM22); //波形を生成するﾓｰﾄﾞを標準ｵｰﾊﾞﾌﾛ,000bitに
  bitclear(TCCR2A,WGM21);
  bitclear(TCCR2A,WGM20);
  bitset(TCCR2B,CS22);    //時間測定分周比を110 = 256分周比に設定。
  bitset(TCCR2B,CS21);
  bitclear(TCCR2B,CS20);
  bitset(TIMSK2,TOIE2); //Timer Inerrupt MaSK2タイマ割込許可レジスタの
                        //最下位bit TimeOverflowInterruptEnableに
                        //１を立ててｵｰﾊﾞﾌﾛ割込を有効にする。
  TCNT2 = 0;
  sei();
}
void loop() {}
ISR(TIMER2_OVF_vect){
  
}
/*
・演習（１）－ timer2_ex1.ino
4 桁の7 セグメントLED 表示器を使用します。(使用箇所は左から３桁まで)
電源を入れた直後、7セグメントLED 表示器に「000」を表示、
その後200m 秒間隔でインクリメントします。
「999」まで表示したら、再度「000」からカウントさせます。
組み込みシステムの動作は、以下の通りです。
ダイナミック点灯時に必要となる表示桁の切り替えにTimer2 を使用する
Timer2 は、
「標準モード」「プリスケーラ：256 分周」「オーバーフロー割込み」で使用する
割り込みハンドラから7セグ表示器の表示関数「seg_disp()」を呼び出す
200msの時間は、「delay」関数を使用する
*/
