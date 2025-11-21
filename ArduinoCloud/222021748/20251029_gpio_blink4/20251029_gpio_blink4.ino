#define LED1 4
#define LED2 5
#define SW1 2
#define COUNT 250000UL
void setup() {
  DDRD |= ((0x1 << LED1)|(0x1 << LED2));
  DDRD &= ~(0x1 << SW1);
  PORTD |= (0x1 << LED1);
}
boolean flag = false;
volatile unsigned long cnt;
void loop() {
  if(!(PIND & (0x1 << SW1))){
    flag = true;
  }
  if(flag){
    PORTD ^=((0x1 << LED1)|(0x1 << LED2));
    for(cnt = 0; cnt < COUNT; cnt++);
  }
}
