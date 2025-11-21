#define CTC_COMP_VALUE 31249
void setup() {
  bitSet(DDRC,PC3); /* PC3(LED)-OUTPUT *///PC3は内部のマクロで3と定義されている
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
  sei(); /* （マスクされていない）全割込み許可 */set status rejister-7bit
}

void loop() {}

ISR(TIMER1_COMPA_vect){
  PORTC ^= (0x1 << PC3); //PORTCの3ビット目=4つ目のビットを反転＝点滅
}