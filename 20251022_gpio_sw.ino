#define PIND (*((volatile unsigned char*)0x29))
#define DDRD (*((volatile unsigned char*)0x2A))
#define PORTD (*((volatile unsigned char*)0x2B))

int main(void){
  DDRD|=(0x1 << 4);
  DDRD&=(~(0x1 << 2));
  for(;;){
    if(PIND&(0x1 << 2)){
      PORTD|=((0x1 << 4));
    }else{
      PORTD&=~(0x1 << 4);
    }
  }
  return 0;
}