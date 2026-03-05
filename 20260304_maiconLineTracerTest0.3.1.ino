/*
◇エラー13：クランクが曲がれない
対処：クランク関数の修正と復活。
◇エラー13.5：右クランクの時に左回転をしてしまい逆走してしまうエラーを発見。
対処：右クランクの時に左回転（左車輪のみ前進）、左クランクの時に右回転（右車輪のみ前進）する設定に変更。
      クランク時も二重ifでチェックする動作に変更。
◇エラー13.6：クランク時に復帰動作をして、前進後退を繰り返して、クランク関数が発動しないことがある。

◇エラー14：手書きごく細コースで復帰動作（後退と前進）を繰り返し進むことが出来ない状況に。
対処：二重ifでcomeback時の首振り角度が少し大きくなったお陰で、前進後退少し首振りを繰り返した後に少しずつ進むことは出来た。
      首振り時の速度をMIN_SPEED 51に。
      ⇒しかしまだ振り切れてしまうことがある。
      ⇒delay(20)からdelay(10)に変更。
◇エラー15：しかし、comeback時の首振りでコースを発見することが出来ずor通りすぎる⇒その後前進⇒白紙の為comeback()を繰り返す動作になってしまった。
対処：首振り時2回目ifの判定条件を緩める＆#define MIN_SPEED 51 //モーターストール状態防止ぎりぎりの出力25％を設置。
      首振り速度を下限値で行う動作へ変更してコース上で止まる確率を高める。
◇エラー16：comeback()復帰動作時の首振り方向が、右小左大右小という順で首を振るので、最初の右小で調節してループから外れ前進を繰り返す動作で、時計周りのクランクおよび急カーブは曲がれるが、
反時計回りのクランクと急カーブが曲がれなくなっている。
void comeback() {
  stop_motor();
  delay(100);
  back_motor();
  delay(300);
  turning_right();
  for (int i = 0; i < 50; i++) {
    read_sensor();
    if (val_C >= GRAY && (val_R >= GRAY || val_L >= GRAY)) {
      ここでif条件がval_C >= GRAY && val_R >=GRAYで達成したら、回転方向フラグを1にして、逆なら回転方向フラグを0にしたまま復帰動作を繰り返す動作にできないだろうか？

20260305_クランク試走エラー
（※⇒＝車体）
⇒■■■■■■■  ←クランク1：右クランク：ここはturn90_left()が発動⇒↘向きに車体がなる⇒comeback()発動で曲がることが出来る。
        ■
        ■
    ■■■■■ ←クランク2：右クランク：ここもクランク1と同様の挙動で曲がることが出来る。
    ■ ↖クランク3：左クランク：ここで前進⇔後退の繰り返しをしてここから先に進むことが出来ない。
    ■
    ■■■■■
◇エラー17：左クランクが曲がることが出来ない。
◇エラー18：クランクでturn90_関数が発動する時としない時の違いが掴めていない。
2026-03-05
◇今日の方針｛
  １．エラー9：グレーライン走行問題への対応。
  ２．エラー10：急カーブへの対応。

★2026年3月5日午後3時47分追記
★どうにも動作がうまくいかないので、修正を昨日に近い状態にまでロールバックし、
★各条件分岐の優先度が異なるif文から、等価優先度のswitch文への改修を行う。
★自力でコードを書き力はほかの人に負けているかも知れないけれど、AIを上手く使う力は
★自分が一番だ。それで良い。
【PJ憲章】
PJ名: ライントレーサー・ステートマシン（状態遷移）化計画

目的(Goal): ロボットの制御を「優先度付きのif文」から「現在の状態（State）に基づくswitch文」に変更し、首振り中であっても瞬時にラインを検知して前進モードに復帰できるようにする。

【コード修正案の目次 (WBS)】
フェーズ1：状態（State）の定義
ロボットが今、何をしているのか（直進中か、右修正中か、コース探索中か）を管理するための「状態変数」と「状態のリスト」を定義します。

フェーズ2：メインループの switch 化（骨組みの作成）
現在の loop() 内にある if-else if の羅列をすべてコメントアウトし、switch (currentState) という骨組みを作ります。これにより、すべての状態が「対等」になります。

フェーズ3：通常走行とズレ修正の移行（遷移条件の実装）
直進、右ズレ修正、左ズレ修正の3つの状態を行き来するロジックを switch 文の中に組み込みます。

フェーズ4：クランク処理の非同期化
turn_90 関数の中にある while （マイコンを硬直させる原因）を解体し、「クランク旋回中」という状態を作ってメインループの中で回るようにします。

フェーズ5：最強の comeback （探索モード）の実装
comeback の中にある for ループや delay の塊（これが「首振り中に前進に戻れない」最大の原因です）を解体し、「探索中」という状態を作ります。首を少し振るたびにメインループに戻ってセンサーを読むため、黒線を踏んだ瞬間に即座に「直進状態」へ復帰できるようになります。

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
#define MIN_SPEED 51  //モーターストール状態防止ぎりぎりの出力25％
//※Read(入力)は2^10の1023までの分解能があるが出力は異なるので注意。
// ピン配置
#define CF_SENSOR A3
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
volatile int val_CF = 0;
volatile unsigned char turnDirectionSW = 0;//首振り動作、右小左大右小を0、左小右大左小を1
volatile int search_step = 0;   // 探索の歩数（カウンタ）
volatile int search_phase = 0;  // 探索の段階（0:後退, 1:右小, 2:左大, 3:右小...など）
//-- ※switch文を使うための準備 ---
enum RobotState{
  STATE_FORWARD,       // 直進状態
  STATE_CORRECT_LEFT,  // 左ズレ修正状態（右に舵を切る）
  STATE_CORRECT_RIGHT, // 右ズレ修正状態（左に舵を切る）
  STATE_TURN_LEFT90_START,  // 左クランク：少し前進して旋回を始める状態
  STATE_TURN_LEFT90_DOING,  // 左クランク：旋回中（白を探して黒を待つ）状態
  STATE_TURN_RIGHT90_START, // 右クランク：少し前進して旋回を始める状態
  STATE_TURN_RIGHT90_DOING, // 右クランク：旋回中（白を探して黒を待つ）状態
  STATE_COMEBACK_START, // 探索開始（準備）
  STATE_COMEBACK_DOING, // 探索実行中
  STATE_STOP_MOTOR    //エンジン停止状態
};
// 現在のロボットの状態を記憶する変数（初期状態は「直進」にしておく）
volatile RobotState currentState = STATE_STOP_MOTOR;


// --- 2. モジュール（機能ごとの関数） ---
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
  analogWrite(RA_ENABLE, 0);
  analogWrite(LB_ENABLE, 0);
}
//モータ後退
void back_motor() {
  digitalWrite(RA_PHASE, 1);
  digitalWrite(LB_PHASE, 0);
  analogWrite(RA_ENABLE, MAX_SPEED);
  analogWrite(LB_ENABLE, MAX_SPEED);
}
//ゆっくり360度旋回※度数を指定した旋回はサーボを使わないと無理なので、
//出力を半分に下げてゆっくり回る方式にしよう。
void turning_right() {
  digitalWrite(RA_PHASE, 1);
  digitalWrite(LB_PHASE, 1);
  analogWrite(RA_ENABLE, MIN_SPEED);
  analogWrite(LB_ENABLE, MIN_SPEED);
}
void turning_left() {
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 0);
  analogWrite(RA_ENABLE, MIN_SPEED);
  analogWrite(LB_ENABLE, MIN_SPEED);
}
void read_sensor() {
  val_L = analogRead(L_SENSOR);
  val_C = analogRead(C_SENSOR);
  val_R = analogRead(R_SENSOR);
}

#if 0
// void comeback() {
//   stop_motor();
//   delay(100);
//       back_motor();
//   for(int i = 0;i<50;i++){
//     read_sensor();
//     if(val_C >=BLACK && (val_L >= BLACK || val_R >= BLACK)){
//       stop_motor();
//       delay(100);
//       return;
//     }
//     delay(10);
//   }
//   if(turnDirectionSW == 0){
//   turning_right();
//   for (int i = 0; i < 50; i++) {
//     read_sensor();
//     if (val_C >= BLACK && val_L >= GRAY) {
//       delay(10);
//       read_sensor();
//       if (val_L >= GRAY) {
//         stop_motor();
//         delay(100);
//         turnDirectionSW = 0;
//         return;
//       }
//     }
//     delay(10);
//   }
//   turning_left();
//   for (int i = 0; i < 100; i++) {
//     read_sensor();
//     if (val_C >= BLACK && val_R >= GRAY) {
//       delay(20);
//       read_sensor();
//       if (val_R >= GRAY) {
//         stop_motor();
//         delay(100);
//         turnDirectionSW = 1;
//         return;
//       }
//     }
//     delay(10);
//   }
//   turning_right();
//   for (int i = 0; i < 50; i++) {
//     read_sensor();
//     if (val_C >= BLACK && val_L >= GRAY) {
//       delay(20);
//       read_sensor();
//       if (val_L >= GRAY) {
//         stop_motor();
//         delay(100);
//         turnDirectionSW = 0;
//         return;
//       }
//     }
//     delay(10);
//   }
//   }else{
//   turning_left();
//   for (int i = 0; i < 50; i++) {
//     read_sensor();
//     if (val_C >= BLACK && val_R >= GRAY) {
//       delay(10);
//       read_sensor();
//       if (val_R >= GRAY) {
//         stop_motor();
//         delay(100);
//         turnDirectionSW = 1;
//         return;
//       }
//     }
//     delay(10);
//   }
//   turning_right();
//   for (int i = 0; i < 100; i++) {
//     read_sensor();
//     if (val_C >= BLACK && val_L >= GRAY) {
//       delay(20);
//       read_sensor();
//       if (val_L >= GRAY) {
//         stop_motor();
//         delay(100);
//         turnDirectionSW = 0;
//         return;
//       }
//     }
//     delay(10);
//   }
//   turning_left();
//   for (int i = 0; i < 50; i++) {
//     read_sensor();
//     if (val_C >= BLACK && val_R >= GRAY) {
//       delay(20);
//       read_sensor();
//       if (val_R >= GRAY) {
//         stop_motor();
//         delay(100);
//         turnDirectionSW = 1;
//         return;
//       }
//     }
//     delay(10);
//   }
//   }
// }
#endif

void comeback_start() {
  stop_motor();
  delay(100);
  back_motor();
  delay(300);
  stop_motor();
  
  search_step = 0;  // 歩数をゼロに
  search_phase = 1; // フェーズ1（右小探索）からスタート
  
  currentState = STATE_COMEBACK_DOING; // 状態を「探索実行中」へ移行！
}

// 探索の実行（メインループのたびに少しずつ首を振る）
void comeback_doing() {
  // ★ ここが最大のポイント！
  // メインループから呼ばれるたびに、センサーをチェック！
  if (val_C >= GRAY && (val_R >= GRAY || val_L >= GRAY)) {
      // ダブルチェック（ノイズ対策）
      delay(20);
      read_sensor();
      if (val_C >= GRAY) {
         stop_motor();
         currentState = STATE_FORWARD; // ★黒を見つけたら即座に直進へ復帰！
         return;
      }
  }

  // 黒が見つからなければ、首振りを継続
  search_step++; // 歩数を1歩進める

  if (search_phase == 1) { // フェーズ1：右に小さく探す（50歩）
    turning_right();
    if (search_step >= 50) {
      search_step = 0;
      search_phase = 2; // 次のフェーズへ
    }
  } 
  else if (search_phase == 2) { // フェーズ2：左に大きく探す（100歩）
    turning_left();
    if (search_step >= 100) {
      search_step = 0;
      search_phase = 3; // 次のフェーズへ
    }
  }
  else if (search_phase == 3) { // フェーズ3：右に戻す（50歩）
    turning_right();
    if (search_step >= 50) {
      search_step = 0;
      search_phase = 4; // 諦めて直進へ
    }
  }
  else {
    // 探索終了（見つからなかった場合）
    stop_motor();
    currentState = STATE_FORWARD; // とりあえず直進に戻して様子を見る
  }
}


// 右へ修正する関数（左輪を速く、右輪を遅く）
//※右センサーが黒白境界
//map(変換したい値, 元の最小値, 元の最大値, 変換後の最小値, 変換後の最大値)を使用する
//mapping＝対応付ける、割り当てるが元。
void gradually_right() {
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);  //モータのどちらか片方を逆転させないと回転方向が一致しない。
  int r_val = analogRead(R_SENSOR);
  int speed_up = map(r_val, 0, 1023, HALF_SPEED, MAX_SPEED);
  int speed_down = map(r_val, 0, 1023, HALF_SPEED, MIN_SPEED);  //反比例させる。
  analogWrite(RA_ENABLE, speed_down);
  analogWrite(LB_ENABLE, speed_up);
}
//左へ修正する関数（左輪を遅く、右輪を速く）
//※左センサーが黒白境界。黒白境界時シリアル値450を想定
void gradually_left() {
  digitalWrite(RA_PHASE, 0);
  digitalWrite(LB_PHASE, 1);  //モータのどちらか片方を逆転させないと回転方向が一致しない。
  int l_val = analogRead(L_SENSOR);
  int speed_up = map(l_val, 0, 1023, HALF_SPEED, MAX_SPEED);
  int speed_down = map(l_val, 0, 1023, HALF_SPEED, MIN_SPEED);  //反比例させる。
  analogWrite(RA_ENABLE, speed_up);
  analogWrite(LB_ENABLE, speed_down);
}
// --- クランク非同期処理 ---

// 左クランク開始（少し前進して、旋回モードに切り替える）
void turn_left90_start() {
  motor_forward();
  delay(150); // ※ここは物理的な車軸合わせなので残してOK
  turning_left();
  currentState = STATE_TURN_LEFT90_DOING; // 状態を「旋回中」に移行！
}

// 左クランク旋回中（whileを使わず、メインループの中で黒線を待つ）
void turn_left90_doing() {
  // センサーはすでにメインループで読まれている
  // 完全に白(WHITE)を通り越して、再び黒(BLACK)を踏んだら旋回完了！
  if (val_L > BLACK) {
    stop_motor();
    currentState = STATE_FORWARD; // 直進モードに復帰！
  }
  // それまでは turning_left() の命令が生きているので回り続ける
}

// 右クランク開始
void turn_right90_start() {
  motor_forward();
  delay(150);
  turning_right();
  currentState = STATE_TURN_RIGHT90_DOING;
}

// 右クランク旋回中
void turn_right90_doing() {
  if (val_R > BLACK) {
    stop_motor();
    currentState = STATE_FORWARD;
  }
}
//左クランク対応関数
// void turn_left90() {
//   /*「→＝車体」
//  □
// →□//車体前方3マスセンサーと考える。
//  □
//   　　　 ■■■■
//       □  ■　白
//    ■■→■■■■　黒
//       □     白
//   　　　  ■■■■//センサークランクを検知
//           ■   黒
//    ■■■■■■→■ 　黒
//           □   白
//   　　　　■■■■
//           ■   黒
//    ■■■■■■■→ 　黒//少し前進して回転軸をコーナ―に合わせる
//               白//白白白になるまでは進まない(進んだら後退するので大丈夫だが時間ロス)
//   　　　  ■■■■
//         白■白//反時計回り、センサーが白黒白になったら1段目のwhile終了      　　
//    ■■■■■■■↑　　
//  　　　　 ■■■■
//         黒■白//左センサーが元居た黒線に触れるまで旋回を続ける（2段目のwhile終了）
//    ■■■■■■■↖　
//                　 　　　　
//   */
//   motor_forward();  //少しだけ前進して、タイヤの回転軸をコーナーに合わせる。
//   delay(150);
//   turning_left();  //左旋回を開始
//   while (analogRead(L_SENSOR) > BLACK) {
//   }  //ソフトウェアでのループとハードウエアのループは連動していない。
//   //マイコンで出力を一度ONにしたら、stopと言うまでONのまま。ラッチ(latch:保持)と言う仕組み。
//   while (analogRead(L_SENSOR) < WHITE) {
//   }
//   stop_motor();
// }
/*必要機能未実装リスト
◆ライン色が灰色箇所での走行モード
◆トンネル内(暗い所)での走行モード
◇徐行走行モード
◇ETCゲート前減速＆停止モード
◇飛び出し時急停止モード
◇細ライン(手書きライン幅5㎜程度)走行モード
◇カーステレオ
◇駐車場での停止
◇モーターサージ対策コンデンサー
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
  pinMode(L_SENSOR, INPUT);
  pinMode(C_SENSOR, INPUT);
  pinMode(R_SENSOR, INPUT);
}
// --- 4. メインループ（トップダウンの論理） ---
void loop() {
  // センサーの値を取得
  val_L = analogRead(L_SENSOR);
  val_C = analogRead(C_SENSOR);
  val_R = analogRead(R_SENSOR);

  // Serial.print("左");
  // Serial.print(val_L);
  // Serial.print("中");
  // Serial.print(val_C);
  // Serial.print("右");
  // Serial.println(val_R);

// ---------------------------------------------------------
  // ★ フェーズ3：状態の遷移（遷移条件の実装）
  // ---------------------------------------------------------
 // ★重要★ 特別なモード（クランクや探索）の最中は、センサーによる強制上書きを無視する！
  if (currentState == STATE_FORWARD || 
      currentState == STATE_CORRECT_LEFT || 
      currentState == STATE_CORRECT_RIGHT ||
      currentState == STATE_STOP_MOTOR) {

      // 【最優先】コースアウト（白白白 または 黒黒黒）
      if ((val_L < WHITE && val_C < WHITE && val_R < WHITE) || (val_L > BLACK && val_C > BLACK && val_R > BLACK)) {
        currentState = STATE_COMEBACK_START;
      }
      // 【優先度2】左クランク検知（左と中央が黒）
      else if (val_L > BLACK && val_C > BLACK) {
        currentState = STATE_TURN_LEFT90_START; 
      }
      // 【優先度3】右クランク検知（右と中央が黒）
      else if (val_R > BLACK && val_C > BLACK) {
        currentState = STATE_TURN_RIGHT90_START; 
      }
      // 【優先度4】右にズレている（ラインは左）
      else if (val_L >= WHITE) {
        currentState = STATE_CORRECT_LEFT;
      }
      // 【優先度5】左にズレている（ラインは右）
      else if (val_R >= WHITE) {
        currentState = STATE_CORRECT_RIGHT;
      }
      // 【優先度6】どれでもない（完璧な直進）
      else {
        currentState = STATE_FORWARD;
      }
  }
  // ※ DOING（実行中）の状態の時は、この判定をスルーして、現在の DOING を維持する。
// 2. 現在の「状態（State）」に応じた行動のみを実行する（フェーズ2の骨組み）
switch(currentState){
    case STATE_FORWARD:
      motor_forward();
      break;
    case STATE_CORRECT_LEFT:
      gradually_left();
      break;
    case STATE_CORRECT_RIGHT:
      gradually_right();
      break;
    case STATE_TURN_LEFT90_START:
      turn_left90_start();
      break;
    case STATE_TURN_LEFT90_DOING:
      turn_left90_doing();
      break;
    case STATE_TURN_RIGHT90_START:
      turn_right90_start();
      break;
    case STATE_TURN_RIGHT90_DOING:
      turn_right90_doing();
      break;
    case STATE_COMEBACK_START:
      comeback_start();
      break;
    case STATE_COMEBACK_DOING:
      comeback_doing();
      break;
    case STATE_STOP_MOTOR: // 停止状態
      stop_motor();
      break;
  }

  delay(10); // ループが暴走しないように少しだけ待つ

#if 0
  // if (val_L < WHITE && val_C < WHITE && val_R < WHITE) {
  //   delay(20);
  //   read_sensor();  // 白白白or黒黒黒コース復帰動作を発動！
  //   if (val_L < WHITE && val_C < WHITE && val_R < WHITE) {
  //     comeback();  // 白白白or黒黒黒コース復帰動作を発動！
  //   }
  // }
  // // 【優先度2】黒黒白左クランクの検知（左と中央が同時に黒）
  // else if (val_L > BLACK && val_C > BLACK) {
  //   delay(20);
  //   read_sensor();
  //   if (val_L > BLACK) {
  //     turn_left90();
  //   }
  // }
  // // 【優先度3】白黒黒右クランクの検知（右と中央が同時に黒）
  // else if (val_R > BLACK && val_C > BLACK) {
  //   delay(20);
  //   read_sensor();
  //   if (val_R > BLACK) {
  //     turn_right90();
  //   }
  // }
  // // 【優先度4】灰黒白通常のライントレース（左ズレ修正）
  // else if (val_L >= WHITE) {
  //   turnDirectionSW = 1;
  //   gradually_left();
  // }
  // // 【優先度5】白黒灰通常のライントレース（右ズレ修正）
  // else if (val_R >= WHITE) {
  //   turnDirectionSW = 0;
  //   gradually_right();
  // }
  // // 【優先度6】白黒白上記のどれでもない（中央だけが黒＝完璧な直進状態）
  // else {
  //   motor_forward();
  // }
  // delay(10);  // ループが暴走しないように少しだけ待つ
#endif
}