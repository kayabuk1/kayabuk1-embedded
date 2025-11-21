#define LED1 4
#define LED2 5
#define LED3 6
#define LED4 7
#define SW1 2
#define COUNT 250000UL

void setup() {
  DDRD |= (0x1 << LED1);
  DDRD |= (0x1 << LED2);
  DDRD |= (0x1 << LED3);
  DDRD |= (0x1 << LED4);
  DDRD &= ~(0x1 << SW1);    
  PORTD &= ~(0x1 << LED2); 
  PORTD &= ~(0x1 << LED3); 
  PORTD &= ~(0x1 << LED4);
  PORTD |= (0x1 << LED1);
  PORTD |= (0x1 << SW1);
}

boolean flag = false;
volatile static unsigned long cnt;

void loop() 
{
  if(!(PIND & (0b00000100))){
    flag=true;
  }
  if(flag){
    for(cnt=0;cnt<250000;cnt++);{
      PORTD = 0b00010000;
    }
    for(cnt=0;cnt<250000;cnt++);{
      PORTD = 0b00100000;
    }
    for(cnt=0;cnt<250000;cnt++);{
      PORTD = 0b01000000;
    }
    for(cnt=0;cnt<250000;cnt++);{
      PORTD = 0b10000000;
    }
  }
 
 }