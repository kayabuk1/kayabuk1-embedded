#define LED_A 0 //PB0
#define LED_B 1 //PB1
#define LED_C 2 //PB2
#define LED_D 3 //PB3
#define LED_E 4    //PD4
#define LED_F 5    //PD5
#define LED_g 6    //PD6
#define LED_DP 7   //PD7
#define DIG1 0          //PC0
#define DIG2 1          //PC1
#define DIG3 2          //PC2
#define SW1 2      //PD2
#define SW2 3      //PD3 

void disp7(char num);

extern unsigned char disp[];
/*
extern
「外部の(external)」という意味の修飾子。
これが付くと、この変数は「この」ﾌｧｲﾙの中には実体がありません。
別のファイルに実体が作られているので、そっちを使ってね。
とコンパイラに伝えることになる。
コンパイラはこれを発見した時点では、配列用にメモリを確保せず、
後でリンカLinkerに探してもらおうとメモを残す。
実体のある7seg.inoを読み込んだ時点で、初めて配列用にメモリを確保する。
そしてコンパイラ翻訳後にリンカがメモをみて、変数と実体をつなげる。
これによって他で作成した変数を、重複して作成せずエラーにならずに済む
*/