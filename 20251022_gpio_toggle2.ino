#define PIND (*(volatile unsigned char*)0x29)
#define DDRD (*(volatile unsigned char*)0x2A)
#define PORTD (*(volatile unsigned char*)0x2B)
#define SW2 3
#define LED3 6

void setup(void);
void loop(void);
int main(void){
  setup();
  for(;;){
    loop();
  }
  return 0;
}
void setup(void){
  DDRD |= 0b11110000;
  DDRD &= 0b11110011;
  PORTD =0x0;
  PORTD |= (0x1 << SW2);
}
boolean flag = true;
void loop(void){
  if(!(PIND & (0x1 << SW2)) && flag){
    PORTD ^= (0x1 << LED3);
    flag = false;
  }
  if(PIND & (0x1 << SW2))
    flag =true;
}