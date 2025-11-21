void setup() {
  bitSet(DDRC, PC3);
  bitClear(DDRD, PD2);
  bitSet(PORTD, PD2);
  bitClear(EICRA, ISC00);
  bitSet(EICRA, ISC01);
  bitSet(EIMSK, INT0);
  bitClear(PORTC, PC3);

}

void loop() {
  

}
ISR(INT0_vect){
  PORTC ^=(0x1 << PC3);
}