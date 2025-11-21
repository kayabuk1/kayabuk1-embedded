#define LED4 7
#define LED3 6
#define COUNT 250000UL

void setup() {
  // put your setup code here, to run once:
  DDRD |= (0x1 << LED4);
  DDRD |= (0x1 << LED3);
  PORTD &= ~(0x1 << LED4);
  PORTD &= ~(0x1 << LED3);
}

void loop() {
  // put your main code here, to run repeatedly:
  volatile static unsigned long cnt = 0;
  if(++cnt >= COUNT){
    cnt = 0;
    PORTD ^= (0x1 << LED4);
  }
  if(++cnt >= COUNT){
    cnt =0;
    PORTD ^=(0x1 << LED3);
  }
}
