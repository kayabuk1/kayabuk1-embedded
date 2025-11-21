void setup() {
  bitSet(DDRC,PC3); // PC3-OUTPUT
  bitSet(DDRC,PC4); // PC4-OUTPUT
  bitSet(DDRC,PC5); // PC5-OUTPUT
  bitSet(PORTD,PD2); // 内部プルアップ有効
  bitSet(PORTD,PD3); // 内部プルアップ有効
  bitClear(EICRA,ISC00); // ISC00=0
  bitSet(EICRA,ISC01); // ISC01=1
  bitSet(EIMSK,INT0); // INT0-ENABLE
  PORTC &= 0x07; // LED-消灯
}
void loop() {
  if(bitRead(PIND,PD3)==0){
    PORTC ^= (0x1 << PC4);
  }
  delay(1000); // Arduino 関数
  PORTC |= (0x1 << PC5);
  delay(1000); // Arduino 関数
  PORTC &= ~(0x1 << PC5);
}
ISR(INT0_vect){
PORTC ^= (0x1 << PC3);
}