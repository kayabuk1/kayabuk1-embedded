#define LED1 4
#define LED2 5
#define LED3 6
#define LED4 7
#define SW1 2

unsigned char cnt;

void setup() {
  DDRD |=0xF0; 
        DDRD &= ~(0x1 << SW1);
        cnt = 0;
}

void delayMs(volatile unsigned long time){
  time*= 2000UL;
          while(time--)asm("nop");
}

void loop() {
  if(!bitRead(PIND,SW1)){
    delayMs(10);
    if(!bitRead(PIND,SW1)){
      if(++cnt >= 16){
        cnt = 0;
      }
      PORTD = (cnt << 4);
      while(!bitRead(PIND,SW1));
      delayMs(10);
    }
  }

}
