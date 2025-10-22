#define LED4 7
#define COUNT 250000UL

void setup() {
  // put your setup code here, to run once:
  DDRD |= (0x1 << LED4);
  DDRD |= (0x1 << 6);
  PORTD &= ~(0x1 << LED4);
}

void delay_time(){
  volatile unsigned long cnt;
  for(cnt=0;cnt<COUNT;cnt++);
}

void loop() {
  // put your main code here, to run repeatedly:
  delay_time();
  PORTD ^=(0x1 << LED4);
  PORTD |=(0x1 << 6);
}
