// ●LED０を点滅する関数
void out_led0(bool on);
// ●LED１を点滅する関数
void out_led1(bool on);
// ●LED２を点滅する関数
void out_led2(bool on);
// ■ボード上のLEDを一斉アクセスする関数　※ビットを使って点滅
void out_led_byte(unsigned char led_byte_data);

// ■スイッチ用の関数
bool get_tactSW0(void);
bool get_tactSW1(void);
bool get_tactSW2(void);
// ■ボード上ﾀｸﾄｽｲｯﾁの状態を一斉に取得して返す関数
unsigned char get_tactSW_byte(void);