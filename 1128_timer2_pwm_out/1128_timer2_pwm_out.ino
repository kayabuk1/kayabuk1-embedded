void setup() {
  bitSet(DDRD,PD3); /* OCR2B:OUTPUT */
  TCCR2A = 0b00100011; /* COM2B:10,WGM:011
    １．出力設定
    Time/CounterControlRegister2Aレジスタの
    CompareOutputMode2B1ビットに１(COM2B0は０)で10＝ｺﾝﾍﾟｱﾏｯﾁLow出力
    ※Timer2はレジスタがTCCR2AとTCCR2Bの2つに跨っていて、
    OC2Bｺﾝﾍﾟｱ出力端子B＝PD3の出力モードを決めるビットはTCCR2Aにある。
    ２．PWMの動作の仕方設定
    WaveformGenerationMode22,21,20ビットを011で8ﾋﾞｯﾄ高速PWMモード */
  TCCR2B = 0b00000100; /* CS2:100 64 分周 
    ClockSelect22,21,20⇒100で64分周*/
  OCR2B = 255; /* Duty:100% 
    OutputCompareRegister2Bｺﾝﾍﾟｱﾏｯﾁで比較する値を255(最大値)に設定*/
}
void loop() {
  static unsigned char comp = 255;  
    //loopごとに255が代入されない様にstatic＝一度だけ255を代入
  --comp; //0になってもデクリメントする⇒0000 0000
                                       -0000 0001
                                        1111 1111 
                                        無いところから無理やり1借りてきて引くのでアンダーフロー※2の補数の為
  TCNT2 = 0;
  OCR2B = comp;
  delay(5);
}