#define OVF_COUNT 122

void setup() 
{
  bitSet(DDRC,PC3); // DDRCでPC3=PORTC3を1、OUTPUTに設定
  PORTC &= 0xF7;    // LED-消灯 11110111　PORTC3の出力をOFF
  cli(); // 全割込み禁止にビットを設定
        //安全な状態にしてタイマﾓｰﾄﾞの設定を行う為に一度禁止する。
    bitClear(TCCR2B,WGM22); // 標準モード選択 
    bitClear(TCCR2A,WGM21); //WGM20,21,22は2つのｶｳﾝﾀ制御ﾚｼﾞｽﾀに跨って
    bitClear(TCCR2A,WGM20); //いる。全て0bitに設定で標準主力ﾓｰﾄﾞになる
  bitSet(TCCR2B,CS22); // プリスケーラ（256 分周）
  bitSet(TCCR2B,CS21);  //TimeCountControlRegister2BのCountSelect
  bitClear(TCCR2B,CS20);  //22,21,20を110に設定するとck1周期が256倍に
  bitSet(TIMSK2,TOIE2); // オーバーフロー割込み許可
        //TimerInterruptMaSKﾚｼﾞｽﾀのTimerOverflowInterruptEnableに
        //1を立てることでｵｰﾊﾞｰﾌﾛｰ割込みが許可される
  TCNT2 = 0; // Timer2 カウンタのクリア
            //TimerCouNT2 ｶｳﾝﾀ情報そのもののﾚｼﾞｽﾀ8bitこれを0でクリア。
            //変数名のように見えるけれど、ﾃﾞｰﾀ型宣言も無しにいきなり0が
            //入ると少し面食らうね。でも大文字だから変数じゃないと
            //気づけるのか。
sei(); // （マスクされていない）全割込み許可
       //なぜ一度cli()全禁止にした後、全許可する必要があるのだろうか？
}
void loop() {//空のプログラムが実行
}
ISR(TIMER2_OVF_vect)
{
  static int cnt = 0;
  if(++cnt >= OVF_COUNT){
    PORTC ^= (0x1 << PC3);
    cnt = 0;
  }/*
  タイマ割り込みであっても、割り込み処理はISR()マクロ関数？の引数？に
  何が要因で割り込み処理が発生するかを記述しないといけないのだね。
  ISR()の動作や仕組みも少し知りたい。
  いま()に入っている割り込みベクターがTIFR2ﾚｼﾞｽﾀのTOVが1になることで、
  {}安易の動作が行われるという理解で良いのかな？
  それとなんでif()内cntは前置ｲﾝｸﾘﾒﾝﾄなんだろうか？
  これもloopのように繰り返されるの？
  */
}