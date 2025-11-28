void setup() {
//bitSet(DDRD,PD3);
bitSet(DDRD,PB3);
TCCR2A = 0b01010010; // コンペアマッチトグル出力、CTC モード
TCCR2B = 0b00000000; // タイマ停止
OCR2A = 24; // コンペアマッチの値(1ms)
}
void loop() {
delay(250);
TCNT2 = 0;
TCCR2B = 0b00000100; // 矩形波出力 64 分周
delay(50);
TCCR2B = 0b00000000; // 出力停止
delay(50);
TCNT2 = 0;
TCCR2B = 0b00000100; // 矩形波出力
delay(50);
TCCR2B = 0b00000000; // 出力停止
}