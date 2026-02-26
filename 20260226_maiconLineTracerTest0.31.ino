/*◆：実施済み、◇：未実施
＜2026-02-25試走1回目log＞
◆エラー1：速度が速すぎて、コースを飛び出す→少しバックする→コースに戻る→コースを順走する→すぐ飛び出すのループになっている。
◇対処案1：速度をすべて1/2にして再度試走してみる。
未実施対処案1－4：Arduino関数ではなく、マイコンと同じレイヤーで直接レジスタを叩くコードに変える
変更1：#define MAX_SPEED 255 → #define MAX_SPEED 127
     #define HALF_SPEED 127 → #define HALF_SPEED 64

◆エラー2：コース脱線時の動きが想定ではその場で1回転→3輪なので、ある程度の輪を描いて走行する動作になっている。
◆エラー3：コース探索時の輪を描いて走行する動きにより、再度コースを発見した時に逆走状態になってしまうことがある。
対処2：1周に10～12秒掛かっている。12/3分割で、4秒旋回→0.1秒止まる→4秒逆旋回→0.1秒止まるのループの首を振る動きで探す動作にしてみる。
//停止→後退→旋回(コース復帰動作)
void comeback(){
  stop_motor();
  delay(100);
  back_motor();
  delay(100); ⇒ delay(500);
  turning_right();
  delay(100); ⇒ delay（500);
追加turning_left();
追加delay(500);
追加turning_right();
追加delay(500);
}
◆エラー4黒黒黒でも走行してしまう。
対処3：黒黒黒でもコース復帰動作をするように変える。
/*if (val_L < WHITE && val_C < WHITE && val_R < WHITE) {
    comeback(); // 白白白コース復帰動作を発動！
  }
  ⇒以下に変更
  if ((val_L < WHITE && val_C < WHITE && val_R < WHITE)||(val_L > BLACK && val_C > BLACK && val_R > BLACK)){
    comeback(); // 白白白or黒黒黒コース復帰動作を発動！
  }
◆エラー5：comeback();関数実行時にコースにセンサーが重なってもすべての操作を終えるまで止まらず、
        動作が終わった時にはまた、白白白でcomeback();を実施という動作になってしまっている。
対処5：while()で括る。
対処：read_sensor(){機能を追加。
void read_sensor(){
  void read_sensor(){
  val_L = analogRead(L_SENSOR);
  val_C = analogRead(C_SENSOR);
  val_R = analogRead(R_SENSOR);
}機能を追加。

◆エラー6：ずっと復帰動作をして前に進めない。
対処6：while文からfor文の細切れループでのセンサー値監視に変更

◆エラー7：これでもずっと首振りしてコースを探す動作が途中で止まらない。なぜだ？
予想原因：センサーの幅が狭すぎた可能性？
BLACKの値も900⇒850に変更してみる。
gradually関数も折角用意したけれどうまく機能できていない？
とりあえず当初の1/4まで全速度を落とした。

◆エラー8：少しでもコースアウトするとバック⇒停止を小刻みに繰り返して前進しない。
WHITEも100⇒150に変更。
 turning_right();
  for(int i = 0; i < 50; i++)
  {
    read_sensor();
    if(val_R > BLACK && val_C > BLACK){
      delay(50);
      return;
    }
    delay(10);loopに戻る値を少し厳しくしてみる。
復帰動作で首を振った後バックした後止まっている時間が長い。
まだ小刻みに復帰動作を繰り返すことが止まらない。
そもそも白紙の上で小刻みにバックするのを繰り返してしまって、首振りも上手くできなくなった。

**原因**：動作をわかりやすくしようとPWM出力を当初の1/4と低くくし過ぎてしまった。
          その結果モータの出力が足りず首を振る動作もできなくなっていた。
対処：#define MAX_SPEED 100  //analogWrite(出力最大値)#define HALF_SPEED 80
      に変更した結果。ストレートおよびゆるやかな～記号の様な曲線は走ることが出来るようになった。
変更：DCモータはPWM出力が30％～25％を下回ると、ストール状態に陥る回転しなくなるとのこと。255*0.25≒64をmap()関数下限に設定。
// 右へ修正する関数（左輪を速く、右輪を遅く）
//※右センサーが黒白境界
//map(変換したい値, 元の最小値, 元の最大値, 変換後の最小値, 変換後の最大値)を使用する
//mapping＝対応付ける、割り当てるが元。
void gradually_right() {
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);//モータのどちらか片方を逆転させないと回転方向が一致しない。
  int r_val = analogRead(R_SENSOR);
  int speed_up = map(r_val,0,1023,HALF_SPEED,MAX_SPEED);◆
  int speed_down = map(r_val,0,1023,HALF_SPEED,64);//反比例させる。
  analogWrite(RA_ENABLE, speed_down);
  analogWrite(LB_ENABLE, speed_up);
}

＜2026-02-26log＞
◆エラー9：グレーの線上で前進後退をピストン運動の様に繰り返しグレーラインからそれ以上進めなくなってしまった。
対処：グレー上でのセンサー値を実測後⇒#define GRAYの値を変更（GRAY 450⇒80に）、併せてWHITEの値も変更（WHITE 150⇒50に）
結果：無事グレーライン走行可能に。トンネル暗条件走行も問題なし。⇒対処済み◆に変更。
13:27:45.813 -> 左39中39右39//グレーライン上
13:27:48.302 -> 左31中31右31//白紙上
13:27:50.788 -> 左31中30右31
13:27:53.245 -> 左30中31右31
13:27:55.722 -> 左31中31右30
13:28:18.038 -> 左37中49右43//グレーライン上
13:28:20.521 -> 左37中48右43//白紙上
⇒センサー部の1kΩ抵抗と直列接続10kΩ可変抵抗の値を（今：最小⇒徐々に）に増やして変化を見てみる。
＜可変抵抗270度中45度程度＞
13:37:14.152 -> 左32中32右31//白紙上
13:37:16.629 -> 左32中32右32
13:37:19.117 -> 左32中32右32
13:37:21.583 -> 左32中32右32
13:37:04.252 -> 左41中45右42//グレーライン上
13:37:06.771 -> 左42中45右41
13:37:09.224 -> 左42中46右41
＜可変抵抗270度中90度程度＞
13:45:09.140 -> 左34中35右34//白紙上
13:45:11.664 -> 左34中34右34
13:45:14.109 -> 左34中34右34
13:45:49.334 -> 左81中118右81//グレーライン上
13:45:51.802 -> 左81中119右80
13:45:54.310 -> 左81中119右81
13:45:56.791 -> 左81中119右80
＜微調整後＞
13:49:28.073 -> 左102中115右93//グレーライン上
13:49:30.589 -> 左102中115右92
13:49:33.045 -> 左102中115右92
13:49:35.529 -> 左102中115右92
13:49:38.012 -> 左109中122右98

2026-02-26log
◇今日の方針｛
  １．エラー9：グレーライン走行問題への対応。
｝

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
#define WHITE 50
#define BLACK 850
#define GRAY 80
#define MAX_SPEED 100  //analogWrite(出力最大値)
#define HALF_SPEED 80
//※Read(入力)は2^10の1023までの分解能があるが出力は異なるので注意。
// ピン配置
#define R_SENSOR A2   //端子25番
#define C_SENSOR A1    //端子24番
#define L_SENSOR A0    //端子23番
#define RA_PHASE 4   //PD4端子6番⇔A_PAHSE/AIN1
#define RA_ENABLE 5  //PD5端子11番PWM制御⇔A_ENABLE/AIN2
#define LB_ENABLE 6  //PD6端子12番PWM制御⇔B_ENABLE/BIN2
#define LB_PHASE 7   //PD7端子13番⇔B_PHASE/BIN1
volatile int val_L = 0;
volatile int val_C = 0;
volatile int val_R = 0;
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
//停止→後退→旋回(コース復帰動作)while⇒for細切れループ監視に変更
void comeback()
{
  stop_motor();
  delay(100);
  back_motor();
  delay(300);
  turning_right();
  for(int i = 0; i < 50; i++)
  {
    read_sensor();
    if(val_C >= GRAY && (val_R >= GRAY|| val_L >= GRAY)){
      stop_motor();
      delay(100);
      return;
    }
    delay(10);
  }
  turning_left();
  for(int i = 0; i < 100; i++)
  {
     read_sensor();
     if(val_C >= GRAY && (val_R >= GRAY || val_L >= GRAY)){
      stop_motor();
       delay(100);
       return;
     }
   delay(10);
  }
  turning_right();
  for(int i = 0; i < 50; i++)
  {
    read_sensor();
    if(val_C >= GRAY && (val_R >= GRAY || val_L >= GRAY)){
      stop_motor();
      delay(100);
      return;
    }
    delay(10);
  }
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
  int speed_down = map(r_val,0,1023,HALF_SPEED,64);//反比例させる。
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
  int speed_down = map(l_val,0,1023,HALF_SPEED,64);//反比例させる。
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
//センサー読み込み機能
void read_sensor(){
  val_L = analogRead(L_SENSOR);
  val_C = analogRead(C_SENSOR);
  val_R = analogRead(R_SENSOR);
}
/*必要機能未実装リスト
・ライン色が灰色箇所での走行モード
・トンネル内(暗い所)での走行モード
・徐行走行モード
・ETCゲート前減速＆停止モード
・飛び出し時急停止モード
・細ライン(手書きライン幅5㎜程度)走行モード
・カーステレオ
・駐車場での停止
*/


// --- 3. 初期設定 ---
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
  pinMode(L_SENSOR,INPUT);
  pinMode(C_SENSOR,INPUT);
  pinMode(R_SENSOR,INPUT);
}
// --- 4. メインループ（トップダウンの論理） ---
void loop() {
  // センサーの値を取得
  val_L = analogRead(L_SENSOR);
  val_C = analogRead(C_SENSOR);
  val_R = analogRead(R_SENSOR);
// /*
  Serial.print("左");
  Serial.print(val_L);
  Serial.print("中");
  Serial.print(val_C);
  Serial.print("右");
  Serial.println(val_R);
// */
if (val_L < WHITE  && val_C < WHITE && val_R < WHITE){
    comeback(); // 白白白or黒黒黒コース復帰動作を発動！
  }
/*if (val_L < WHITE && val_C < WHITE && val_R < WHITE) {
    comeback(); // 白白白コース復帰動作を発動！
  }⇒以下に変更
  if ((val_L < WHITE && val_C < WHITE && val_R < WHITE)||(val_L > BLACK && val_C > BLACK && val_R > BLACK)){
    comeback(); // 白白白or黒黒黒コース復帰動作を発動！
  }
*/  
  // 【優先度2】黒黒白左クランクの検知（左と中央が同時に黒）
  /*else if (val_L > BLACK && val_C > BLACK) {
    turn_left90();
  }
  // 【優先度3】白黒黒右クランクの検知（右と中央が同時に黒）
  else if (val_R > BLACK && val_C > BLACK) {
    turn_right90();
  }*/
  // 【優先度4】灰黒白通常のライントレース（左ズレ修正）
  else if (val_L >= WHITE) {
    gradually_left();
  }
  // 【優先度5】白黒灰通常のライントレース（右ズレ修正）
  else if (val_R >= WHITE) {
    gradually_right();
  }
  // 【優先度6】白黒白上記のどれでもない（中央だけが黒＝完璧な直進状態）
  else {
    motor_forward();
  }
  delay(10);  // ループが暴走しないように少しだけ待つ
}