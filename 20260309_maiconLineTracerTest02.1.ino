// ==========================================
// ライントレーサー 制御プログラム
// ==========================================
// -－ピン割り当て表
// *　１｜RESET:
// *　２｜0,RXD:
// *　３｜1,TXD:
// 　４｜2,PD2,INT0:
// 　５｜3,PD3,INT1,PWM:
// *　６｜4,PD4:RA_PHASE
// *　７｜Vcc
// *　８｜GND
// *　９｜OSC1,PB6:水晶振動子16MHz
// *１０｜OSC2,PB7:水晶振動子16MHz
// *１１｜5,PD5,PWM:RA_ENABLE
// *１２｜6,PD6,PWM:LB_ENABLE
// *１３｜7,PD7:LB_PAHSE
// １４｜8,PB0
// ‐‐‐―――折り返し
// １５｜9,PB1,PWM:
// １６｜10,PB2,PWM:
// １７｜11,PB3,PWM:
// １８｜12,PB4:
// １９｜13,PB5,SCK:
// ２０｜VCC:
// ２１｜ARFE:
// ２２｜GND:
// *２３｜PC0,A0:L_SENSOR
// *２４｜PC1,A1:C_SENSOR
// *２５｜PC2,A2:R_SENSOR
// ２６｜PC3,A3:
// ２７｜PC4,A4,SDA:
// ２８｜PC5,A5,SCL:

#define WHITE 220
#define BLACK 800
#define GRAY 400
#define MAX_SPEED 120  //analogWrite(出力最大値)
#define HALF_SPEED 86
#define MIN_SPEED 51  //モーターストール状態防止ぎりぎりの出力25％
//※Read(入力)は2^10の1023までの分解能があるが出力は異なるので注意。
// ピン配置
#define R_SENSOR A2  //端子25番
#define C_SENSOR A1  //端子24番
#define L_SENSOR A0  //端子23番
#define RA_PHASE 4   //PD4端子6番⇔A_PAHSE/AIN1
#define RA_ENABLE 5  //PD5端子11番PWM制御⇔A_ENABLE/AIN2
#define LB_ENABLE 6  //PD6端子12番PWM制御⇔B_ENABLE/BIN2
#define LB_PHASE 7   //PD7端子13番⇔B_PHASE/BIN1
volatile int val_L = 0;
volatile int val_C = 0;
volatile int val_R = 0;

enum RobotState {            //RobotStateという名前付き整数リストを作成。
  STATE_FORWARD,             // 0：直進状態
  STATE_CORRECT_LEFT,        // 1：左ズレ1:修正状態（右に舵を切る）
  STATE_CORRECT_RIGHT,       // 2：右ズレ修正状態（左に舵を切る）
  STATE_TURN_LEFT90_START,   // 3：左クランク：少し前進して旋回を始める状態
  STATE_TURN_LEFT90_DOING,   // 4：左クランク：旋回中（白を探して黒を待つ）状態
  STATE_TURN_RIGHT90_START,  // 5：右クランク：少し前進して旋回を始める状態
  STATE_TURN_RIGHT90_DOING,  // 6：右クランク：旋回中（白を探して黒を待つ）状態
  STATE_COMEBACK_START,      // 7：探索開始（準備）
  STATE_COMEBACK_DOING,      // 8：探索実行中
   STATE_ALIGN_TO_LINE,   // ★追加：復帰後の向き揃え状態
  STATE_STOP_MOTOR           //9：エンジン停止状態
};
volatile RobotState currentState = STATE_STOP_MOTOR;
// --- 2. モジュール（機能ごとの関数） ---
void read_sensor() {
  val_L = analogRead(L_SENSOR);
  val_C = analogRead(C_SENSOR);
  val_R = analogRead(R_SENSOR);
}
// モーター前進関数
void motor_forward() {
  // ここに左右のモーターを前進させる処理を書く
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);  //モータのどちらか片方を逆転させないと回転方向が一致しない。
  analogWrite(RA_ENABLE, MAX_SPEED);
  analogWrite(LB_ENABLE, MAX_SPEED);
}
// モーター停止（ショートブレーキ）関数
void stop_motor() {
  // ここにHブリッジを両方HIGHにする処理を書く
  digitalWrite(RA_PHASE,0);
  digitalWrite(LB_PHASE,0);
  analogWrite(RA_ENABLE, 0);  // DRV8835はENABLE=0がブレーキ
  analogWrite(LB_ENABLE, 0);
}
//モータ後退
void back_motor() {
  digitalWrite(RA_PHASE, 1);
  digitalWrite(LB_PHASE, 0);
  analogWrite(RA_ENABLE, MAX_SPEED);
  analogWrite(LB_ENABLE, MAX_SPEED);
}
void gradually_right() {
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);  //モータのどちらか片方を逆転させないと回転方向が一致しない。
  int r_val = analogRead(R_SENSOR);
  int speed_up = map(r_val, WHITE, BLACK, HALF_SPEED, MAX_SPEED);
  int speed_down = map(r_val, WHITE, BLACK, HALF_SPEED, MIN_SPEED);  //反比例させる。
  analogWrite(RA_ENABLE, speed_down);
  analogWrite(LB_ENABLE, speed_up);
}
//左へ修正する関数（左輪を遅く、右輪を速く）
//※左センサーが黒白境界。黒白境界時シリアル値450を想定
void gradually_left() {
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);  //モータのどちらか片方を逆転させないと回転方向が一致しない。
  int l_val = analogRead(L_SENSOR);
  int speed_up = map(l_val, WHITE, BLACK, HALF_SPEED, MAX_SPEED);
  // 白：HALF_SPEED(86) ～ 黒：MAX_SPEED(120)
  int speed_down = map(l_val, WHITE, BLACK, HALF_SPEED, MIN_SPEED); 
  // 白：HALF_SPEED(86) ～ 黒：MIN_SPEED(51) //反比例させる。
  analogWrite(RA_ENABLE, speed_up);
  analogWrite(LB_ENABLE, speed_down);
}
void setup() {
  Serial.begin(9600);
  // ピンの入出力設定をここに書く
  // センサーピンはアナログ「入力」なので設定不要（または INPUT と明記しても良い）
  // モーター制御ピンを「出力 (OUTPUT)」に設定する
  // ※DDRDを直接叩く代わりに、pinModeでカプセル化して記述。
  pinMode(RA_PHASE, OUTPUT);
  pinMode(RA_ENABLE, OUTPUT);
  pinMode(LB_ENABLE, OUTPUT);
  pinMode(LB_PHASE, OUTPUT);
  pinMode(L_SENSOR, INPUT);
  pinMode(C_SENSOR, INPUT);
  pinMode(R_SENSOR, INPUT);
}
// --- 4. メインループ（トップダウンの論理） ---
void loop() {  //初回起動時の動作順番を〇付数字で辿れる様に書き記す(例：➊)
  //➊ともかく判断の根拠になるセンサーの値を読み込まないと何もできないので、まず読む。
  // センサーの値を取得
read_sensor();

Serial.print("左");
Serial.print(val_L);
Serial.print("中");
Serial.print(val_C);
Serial.print("右");
Serial.println(val_R);

if (currentState == STATE_FORWARD || currentState == STATE_CORRECT_LEFT
 || currentState == STATE_CORRECT_RIGHT || currentState == STATE_STOP_MOTOR) {

  // 【最優先】コースアウト（白白白 または 黒黒黒）
  if ((val_L < WHITE && val_C < WHITE && val_R < WHITE)
      || (val_L > BLACK && val_C > BLACK && val_R > BLACK)) {
    currentState = STATE_COMEBACK_START;
  }
  // 【優先度2】左クランク検知（左と中央が黒）
  else if (val_L > BLACK && val_C > BLACK) {
    currentState = STATE_TURN_LEFT90_START;
  }
  // 【優先度3】右クランク検知（右と中央が黒）
  else
   if (val_R > BLACK && val_C > BLACK) {
    currentState = STATE_TURN_RIGHT90_START;
  }
// 修正後：左右の差分でどちらにズレているか対称的に判断
else if (val_L >= WHITE || val_R >= WHITE) {
  int diff = val_L - val_R;  // 正 = 左が白寄り = 右にズレている
                              // 負 = 右が白寄り = 左にズレている
  if (diff > 50) {            // 左センサーが明らかに白寄り
    currentState = STATE_CORRECT_LEFT;
  } else if (diff < -50) {    // 右センサーが明らかに白寄り
    currentState = STATE_CORRECT_RIGHT;
  } else {
    // 差が小さい（ノイズ）= どちらでもない → 直進維持
    currentState = STATE_FORWARD;
  }
}

switch (currentState) {  //➌状態がSTATE_FORWORDなのでmotor_forward関数を呼び出し
  case STATE_FORWARD:
    motor_forward();
    break;
  case STATE_CORRECT_LEFT:
    gradually_left();
    break;
  case STATE_CORRECT_RIGHT:
    gradually_right();
    break;
  // case STATE_TURN_LEFT90_START:
  //   turn_left90_start();
  //   break;
  // case STATE_TURN_LEFT90_DOING:
  //   turn_left90_doing();
  //   break;
  // case STATE_TURN_RIGHT90_START:
  //   turn_right90_start();
  //   break;
  // case STATE_TURN_RIGHT90_DOING:
  //   turn_right90_doing();
  //   break;
  // case STATE_COMEBACK_START:
  //   comeback_start();
  //   break;
  // case STATE_COMEBACK_DOING:
  //   comeback_doing();
  //   break;
  // case STATE_ALIGN_TO_LINE:
  // align_to_line();
  //   break;
  case STATE_STOP_MOTOR:  // 停止状態
    stop_motor();
    break;
}

delay(10);  // ループが暴走しないように少しだけ待つ
}
}
  