/**
 * @file  board.h
 * @brief 2026年モデルの基板のピンと、機体の定数をまとめたファイルです。
 *
 * 出どころは、ロボット仕様書（20261122）の表1-5「マイコンのピンアサイン」です。
 * 基板や配線が変わったら、このファイルだけを直します。
 *
 * フラッシュメモリ（AT25SF321B）のピンだけは、at25sf/at25sf.h にあります
 * （SPI0：SCK=GP2、MOSI=GP3、MISO=GP4、CS=GP5。CMake で PICO2350=1 を指定しています）。
 */
#ifndef BOARD_H
#define BOARD_H

/* ===== ピン ================================================================ */
#define PIN_UART_TX      0  /* ソケット（UART）                               */
#define PIN_UART_RX      1
/*      GP2〜GP5            フラッシュメモリ（SPI0）。at25sf/at25sf.h を参照  */
#define PIN_ENC_L_A      6  /* 左エンコーダ A相（B相は次の番号の GP7）        */
#define PIN_ENC_R_A      8  /* 右エンコーダ A相（B相は次の番号の GP9）        */
#define BOARD_SPI1_PORT  spi1
#define PIN_SPI1_SCK     10 /* MCP3008 と L3GD20 で共用                       */
#define PIN_SPI1_MOSI    11
#define PIN_SPI1_MISO    12
#define PIN_GYRO_CS      13 /* L3GD20 の CS                                   */
#define PIN_ADC_CS       14 /* MCP3008 の CS                                  */
#define PIN_SW0          15 /* タクトスイッチ0                                */
#define PIN_SW1          16 /* タクトスイッチ1                                */
#define PIN_LED0         17 /* デバッグ用のLED                                */
#define PIN_LED_ONBOARD  25 /* Pico 2 の基板の上にあるLED（ピン表には無い。プログラムが動いている印に使う） */
#define PIN_MOTOR_L_IN1  18 /* TB67H450 左 IN1（PWM1A）                       */
#define PIN_MOTOR_L_IN2  19 /* TB67H450 左 IN2（PWM1B）                       */
#define PIN_MOTOR_R_IN1  20 /* TB67H450 右 IN1（PWM2A）                       */
#define PIN_MOTOR_R_IN2  21 /* TB67H450 右 IN2（PWM2B）                       */
#define PIN_BUZZER       22 /* ブザー（PWM3A）                                */
#define PIN_LINE_LED     26 /* ラインセンサーのLEDのスイッチ                  */
#define PIN_CHECK        27 /* チェック端子（オシロスコープで割り込みの時間を見る） */
#define PIN_VBAT         28 /* 電源電圧（10kΩ と 2.2kΩ で分圧）               */
#define VBAT_ADC_CH      2  /* GP28 は、Picoの中のA/D変換の2番                */

/* ===== 向きと論理 ==========================================================
 * 仕様書からは読み取れなかった所です。動作確認メニュー（'h'）で確かめて、
 * 合っていなければ、ここの値を変えます。
 */
#define SW_PRESSED_LEVEL  0    /* スイッチを押したときのピンの値。0 = 押すとLOW（Picoの中のプルアップを使う） */
#define LINE_LED_ON_LEVEL 1    /* ラインセンサーのLEDが点くときの GP26 の値                                   */
#define MOTOR_L_SIGN      (+1) /* duty を正にしたとき、左の車輪が前進の向きに回らなければ (-1) にする          */
#define MOTOR_R_SIGN      (-1) /* 同じく、右の車輪。右のモーターは左と向かい合わせに付くので、逆にしてあります。
                                  前進の向きに回らなければ (+1) にする                                        */
#define ENC_L_SIGN        (+1) /* 前進したとき、左エンコーダの数が減るなら (-1) にする                         */
#define ENC_R_SIGN        (-1) /* 同じく、右エンコーダ（先生のサンプルは、右の符号を逆にしていました）         */
#define LINE_DISP_SIGN    (+1) /* 機体がラインの右にずれたとき、ずれの値が負になるなら (-1) にする             */

/* ===== センサーの番号（仕様書 1.5.5） ====================================== */
#define SENSOR_NUM        8 /* A/D変換ICの CH0〜CH7                                 */
#define LINE_SENSOR_NUM   6 /* sensor[0]〜[5] がラインセンサー（[5]が左、[0]が右）  */
#define SENSOR_GOAL       6 /* sensor[6] はスタート／ゴールのマーカー（右側）       */
#define SENSOR_CORNER     7 /* sensor[7] はコーナーのマーカー（左側）               */
/* sensor[0]〜[7] が、A/D変換ICの何番のチャンネルにつながっているか。
 * 動作確認の `3` で並びが違っていたら、ここの数字を入れ替えます（プログラムのほかの所は変えなくて済みます）。
 */
#define SENSOR_ADC_CH     {0, 1, 2, 3, 4, 5, 6, 7}

/* ===== 機体の定数（仕様書） ================================================ */
#define TIRE_DIAMETER_M   0.080f   /* タイヤの外径 [m]（表1-3）                        */
#define TREAD_M           0.117f   /* 左右の車輪の間の距離 [m]（表1-3）                */
#define ENC_PULSE_PER_REV 840      /* タイヤ1回転あたりのエンコーダの数（3.2.3）       */
#define GEAR_RATIO        30.0f    /* 減速比（表1-4）                                  */
#define MOTOR_KE          0.00382f /* 逆起電力定数 [V/(rad/s)]（4.1.5）                */
#define VBAT_R_TOP        10000.0f /* 分圧の抵抗、電池側 [Ω]（3.4）                    */
#define VBAT_R_BOTTOM     2200.0f  /* 分圧の抵抗、GND側 [Ω]                            */
#define VBAT_NOMINAL_V    7.4f     /* 電池の定格の電圧 [V]                             */
#define VBAT_PRESENT_V    3.0f     /* 電源電圧がこれより低ければ「電池をつないでいない（USBだけ）」と見なす [V] */
#define VBAT_LOW_V        6.6f     /* 電池をつないでいて、これより低ければ、走行を始めない [V]。
                                      2セルのリチウムポリマー電池は、使い切ると傷みます（仕様書 1.2）。
                                      0 にすると、この確認をしません                   */

#endif /* BOARD_H */
