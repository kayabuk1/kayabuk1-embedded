// ==========================================
// ライントレーサー 制御プログラム
// ==========================================
/*-－ピン割り当て表
*　１｜RESET:
*　２｜0,RXD:
*　３｜1,TXD:
　４｜2,PD2,INT0:
　５｜3,PD3,INT1,PWM:
*　６｜4,PD4:RA_PHASE
*　７｜Vcc
*　８｜GND
*　９｜OSC1,PB6:水晶振動子16MHz
*１０｜OSC2,PB7:水晶振動子16MHz
*１１｜5,PD5,PWM:RA_ENABLE
*１２｜6,PD6,PWM:LB_ENABLE
*１３｜7,PD7:LB_PAHSE
１４｜8,PB0
‐‐‐―――折り返し
１５｜9,PB1,PWM:
１６｜10,PB2,PWM:
１７｜11,PB3,PWM:
１８｜12,PB4:
１９｜13,PB5,SCK:
２０｜VCC:
２１｜ARFE:
２２｜GND:
*２３｜PC0,A0:L_SENSOR
*２４｜PC1,A1:C_SENSOR
*２５｜PC2,A2:R_SENSOR
２６｜PC3,A3:
２７｜PC4,A4,SDA:
２８｜PC5,A5,SCL:
*/
// --- 1. 定数の定義（マジックナンバーの排除） ---
#define WHITE 100
#define BLACK 900
#define MAX_SPEED 255  //analogWrite(出力最大値)
#define HALF_SPEED 127
//※Read(入力)は2^10の1023までの分解能があるが出力は異なるので注意。
// ピン配置
#define R_SENSOR A2   //端子25番
#define C_SENSOR A1    //端子24番
#define L_SENSOR A0    //端子23番
#define RA_PHASE 4   //PD4端子6番⇔A_PAHSE/AIN1
#define RA_ENABLE 5  //PD5端子11番PWM制御⇔A_ENABLE/AIN2
#define LB_ENABLE 6  //PD6端子12番PWM制御⇔B_ENABLE/BIN2
#define LB_PHASE 7   //PD7端子13番⇔B_PHASE/BIN1
// --- 2. モジュール（機能ごとの関数） ---
// モーター前進関数
void motor_forward() {
  // ここに左右のモーターを前進させる処理を書く
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);//モータのどちらか片方を逆転させないと回転方向が一致しない。
  analogWrite(RA_ENABLE, MAX_SPEED);
  analogWrite(LB_ENABLE, MAX_SPEED);
}
// モーター停止（ショートブレーキ）関数
void stop_motor() {
  // ここにHブリッジを両方HIGHにする処理を書く
  analogWrite(RA_ENABLE,0);
  analogWrite(LB_ENABLE,0);
}
//モータ後退
void back_motor(){
  digitalWrite(RA_PHASE,1);
  digitalWrite(LB_PHASE,0);
  analogWrite(RA_ENABLE,MAX_SPEED);
  analogWrite(LB_ENABLE,MAX_SPEED);
}
//ゆっくり360度旋回※度数を指定した旋回はサーボを使わないと無理なので、
//出力を半分に下げてゆっくり回る方式にしよう。
void turning_right(){
  digitalWrite(RA_PHASE,0);
  digitalWrite(LB_PHASE,0);
  analogWrite(RA_ENABLE,HALF_SPEED);
  analogWrite(LB_ENABLE,HALF_SPEED);
}
void turning_left(){
  digitalWrite(RA_PHASE,1);
  digitalWrite(LB_PHASE,1);
  analogWrite(RA_ENABLE,HALF_SPEED);
  analogWrite(LB_ENABLE,HALF_SPEED);
}
//停止→後退→旋回(コース復帰動作)
void comeback(){
  stop_motor();
  delay(100);
  back_motor();
  delay(100);
  turning_right();
  delay(100);
}
// 右へ修正する関数（左輪を速く、右輪を遅く）
//※右センサーが黒白境界
//map(変換したい値, 元の最小値, 元の最大値, 変換後の最小値, 変換後の最大値)を使用する
//mapping＝対応付ける、割り当てるが元。
void gradually_right() {
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);//モータのどちらか片方を逆転させないと回転方向が一致しない。
  int r_val = analogRead(R_SENSOR);
  int speed_up = map(r_val,0,1023,HALF_SPEED,MAX_SPEED);
  int speed_down = map(r_val,0,1023,HALF_SPEED,0);//反比例させる。
  analogWrite(RA_ENABLE, speed_down);
  analogWrite(LB_ENABLE, speed_up);
}
//左へ修正する関数（左輪を遅く、右輪を速く）
//※左センサーが黒白境界。黒白境界時シリアル値450を想定
void gradually_left(){
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);//モータのどちらか片方を逆転させないと回転方向が一致しない。
  int l_val = analogRead(L_SENSOR);
  int speed_up = map(l_val,0,1023,HALF_SPEED,MAX_SPEED);
  int speed_down = map(l_val,0,1023,HALF_SPEED,0);//反比例させる。
  analogWrite(RA_ENABLE, speed_up);
  analogWrite(LB_ENABLE, speed_down); 
}
//右クランク対応関数
void turn_right90(){
  motor_forward();//少しだけ前進して、タイヤの回転軸をコーナーに合わせる。
  delay(150);
  turning_right();//右旋回を開始
  while(analogRead(R_SENSOR) > BLACK){
  }//ソフトウェアでのループとハードウエアのループは連動していない。
  //マイコンで出力を一度ONにしたら、stopと言うまでONのまま。
  while(analogRead(R_SENSOR) < WHITE){
  }
  stop_motor();
}
//左クランク対応関数
void turn_left90(){
  /*「→＝車体」
 □
→□//車体前方3マスセンサーと考える。
 □
  　　　 ■■■■
      □  ■　白
   ■■→■■■■　黒
      □     白
  　　　  ■■■■//センサークランクを検知
          ■   黒
   ■■■■■■→■ 　黒
          □   白
  　　　　■■■■
          ■   黒
   ■■■■■■■→ 　黒//少し前進して回転軸をコーナ―に合わせる
              白//白白白になるまでは進まない(進んだら後退するので大丈夫だが時間ロス)
  　　　  ■■■■
        白■白//反時計回り、センサーが白黒白になったら1段目のwhile終了      　　
   ■■■■■■■↑　　
 　　　　 ■■■■
        黒■白//左センサーが元居た黒線に触れるまで旋回を続ける（2段目のwhile終了）
   ■■■■■■■↖　
               　 　　　　
  */
  motor_forward();//少しだけ前進して、タイヤの回転軸をコーナーに合わせる。
  delay(150);
  turning_left();//左旋回を開始
  while(analogRead(L_SENSOR) > BLACK){
  }//ソフトウェアでのループとハードウエアのループは連動していない。
  //マイコンで出力を一度ONにしたら、stopと言うまでONのまま。ラッチ(latch:保持)と言う仕組み。
  while(analogRead(L_SENSOR) < WHITE){
  }
  stop_motor();
}
// --- 3. 初期設定 ---
void setup() {
  Serial.begin(9600);
  // ピンの入出力設定をここに書く
}
// --- 4. メインループ（トップダウンの論理） ---
void loop() {
  // センサーの値を取得
  int val_L = analogRead(PIN_SENSOR_L);
  int val_C = analogRead(PIN_SENSOR_C);
  int val_R = analogRead(PIN_SENSOR_R);

  // 【フェーズ1: とりあえず走るロジック】
  if (val_L < THRESHOLD_WHITE && val_C < THRESHOLD_WHITE && val_R < THRESHOLD_WHITE) {
    // 全白：とりあえず停止
    motor_stop();
  } else if (val_R > THRESHOLD_BLACK) {
    // 右が黒：右にズレているので右へ修正
    turn_right();
  }
  // ※ここに左修正や直進を追加していく

  delay(10);  // 少し待つ（チャタリング/ノイズ対策）
}