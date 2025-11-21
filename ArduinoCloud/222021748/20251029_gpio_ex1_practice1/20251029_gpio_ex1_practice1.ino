#define LED1 4
#define LED2 5
#define LED3 6
#define LED4 7
#define SW1 2

void setup() {
  DDRD |= (0x1 << LED1);
  DDRD |= (0x1 << LED2);
  DDRD |= (0x1 << LED3);
  DDRD |= (0x1 << LED4);
  DDRD &= ~(0x1 << SW1);    
  PORTD &= ~(0x1 << LED2);  //0000 1000⇒1111 0111
  PORTD &= ~(0x1 << LED4);
  PORTD |= (0x1 << LED1);
  PORTD |= (0x1 << LED3);
  PORTD |= (0x1 << SW1);
}

void loop() {
  if(!(PIND & (0x1 << SW1))){
    PORTD &= ~((0x1 << LED1) | (0x1 << LED3));
    PORTD |= ((0x1 << LED2) | (0x1 << LED4));
  }
  else{
    PORTD |= ((0x1 << LED1) | (0x1 << LED3));
    PORTD &= ~((0x1 << LED2) | (0x1 << LED4));
  }
   }