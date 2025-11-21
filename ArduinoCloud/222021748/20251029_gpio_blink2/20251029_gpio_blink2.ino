#define LED4 7
#define COUNT 250000UL

void setup(){
  DDRD |=(0x1 << LED4);
  PORTD &= ~(0x1 << LED4);
}
void delay_time(){
  volatile unsigned long cnt;
  for(cnt=0;cnt<COUNT;cnt++);
}
void loop(){
  delay_time();
  PORTD ^=(0x1 << LED4);
}