#if 0 // ◆こちらのファイルはコードを作成する際に書いた解説など雑多なコメントが含まれたコード。
// 　完成後こちらも内容整理予定。

/*◆やることリスト（一部学習として身に着けておきたいことも含む）
抜けているもの：ステップ0（v1の完成）
    済：●実機で確認する（ゴースト、シフト方向、スクロール）
    済：●動画かGIFを撮る
    一旦済：●README を書く
●動作トレースや原理などのコメントの追記修正

★miskaiフォントの英数字が半角で幅が4前後で文字と文字間の空白が大きくなって
　しまっている問題。
	済★misakiフォントのクレジットを記載する。
    済●led_buffer_shift_colsの向きがコメントと逆
    済●時間の計算が、定数を変えると止まる作り
    済●ゴースト（残像）が出る可能性⇒blanking消灯関数をhw部にも作成
　  一旦済●コメントと実際の値が合っていない箇所の修正
　  済●先頭のグローバル変数duration_ms、interval_ms、timesは使われておらず、
　　    関数の引数と同じ名前。紛らわしいので削除できる。
    済●led_render_test_one_by_oneは「render」の誤字で、
　　    test_accumulate_dots_animationとほぼ同じ内容。
　　　  ⇒累積1ドット点灯と非累積1ドット点灯で実際に動かすと点灯の仕方がまるで違って興味深いので残しておく。
        ⇒削除。累積と非累積の比較は test_accumulate_dots_animation と test_one_by_one_dot_animation で確認できる
    済●led_buffer_push_col_from_leftも「right」の誤字。

◆ビットフィールドの並び順（x0がbit0になるか）は、C言語の規格ではコンパイラ次第。
今の環境（GCC、ARM）では動きるが、移植性はない。
◆根拠
C言語の国際規格（ISO/IEC 9899、いわゆるC99/C11/C23など）および
日本産業規格（JIS X 3010）において、ビットフィールドの割り付け順序は
「処理系定義（Implementation-defined）」と明記。
※具体的な動作を「その処理系（コンパイラ等）の実装に委ねる」⇒そのコンパイラ作る人が自由に決めて良い。
規格の具体的な記載（JIS X 3010 より抜粋）
「単位内のビットフィールド割付けの順序（上位から下位か、又は下位から上位か）は、処理系定義とする。」
※「GCC + ARM」の組み合わせでは、「下位ビット（bit0）から順に配置する
（リトルエンディアン的配置）」というルールが採用。
◆上記「環境」とは
[1. CPU（アーキテクチャ）] ── [2. ABI（規約）] ── [3. コンパイラ]
１．CPU（アーキテクチャ）のエンディアン
メモリやレジスタ内でのデータの並び順。これに引きずられてビットフィールドの順序の傾向が決まることが多い。
• リトルエンディアン： 下位バイト（ビット）から順に並べる方式。x86/x64、多くのARM（標準設定）など。
• ビッグエンディアン： 上位バイト（ビット）から順に並べる方式。メインフレーム、古いPowerPC、ネットワークプロトコル（TCP/IP）など。
２． ABI（Application Binary Interface）
「環境」を決定づける最も重要なルール。CPUが同じでも、OSやプラットフォームごとにデータの配置ルール
（アライメントやビットフィールドの詰め方）を統一するための共通規格（ABI）が存在。
• AAPCS（ARM Architecture Procedure Call Standard）： ARM向けコンパイルの多くが準拠するABI。
ここでビットフィールドのパッキング規則が規定されているため、
GCCでもClangでも、ARM向けであれば同じ構造体レイアウトになる。
• System V ABI： LinuxなどのUnix系OSで広く使われる規格。
• Microsoft Windows ABI： Windows環境の独自の規則。
３． コンパイラの種類とオプション
最終的にコードを出力するコンパイラ（GCC、Clang、MSVC、各マイコンメーカーの独自コンパイラ）の実装。
同じGCCであっても、コンパイルオプション（-mbig-endian など）によってエンディアンやビットフィールドの並び順が
ガラリと変わる環境もある。
※まとめと対策
ビットフィールドを使った構造体を、「そのままハードウェアのレジスタ（メモリマップドI/O）にマッピングする」、
あるいは「そのまま通信データとして送受信する」コードは、環境が変わったときに動作の保証がなくなる。
もし他環境への移植や、将来的なコンパイラ変更に耐えうるコードにする場合は、ビットフィールド構造体を廃止し、
通常の uint32_t 等の変数に対して シフト演算（>>, <<）とビットマスク（&, |）を使って手動でアクセスするのが
最も安全で移植性の高い標準的なアプローチ。
実際のコードをどう書き換えるべきか、あるいは現在の環境のまま移植性を担保するコンパイラ特有の指定方法（属性など）
◆set_pixelは、switchの代わりにビット演算で書ける。
⇒switch文の部分の修正はする。
ただ、ビット演算はイメージがしにくい部分があるので、
また授業で同級生にコードを配布する可能性があるので可読性の高いスイッチ文のコードを残しておいて、
下にコメントの形で実装する。下のコードをコメントを外し
switch版をコメントアウトしててもらう。
ビット演算版の利点は、bytes[y] を扱う他の関数と書き方がそろうこと、短く書けること。
訓練校での環境は皆同じPicoなので問題ないと考える。
⇒#define USE_BIT_SHIFT 0   // 1にするとビット演算版に切り替わる仕組みに変更。

◆下のあたりのハードウェアアトミックなレジスタ操作を組み込んでより安全に高速に。
    gpio_set_mask();
    //  uint32_t mask 32個のピンを「1つのレジスタ」で一括管理している
    // 00000000 00000000 00000000 00000001 (1) ＝ GPIO0
    // 00000000 00000000 00000000 00000010 (2) ＝ GPIO1
    gpio_clr_mask();
    gpio_put_masked();

hw_clear_bits()
hw_set_bits() が安全な理由（ハードウェアによるアトミック性）
RP2040には、通常のレジスタアドレスに対して、以下のような特殊なアドレス（別名：エイリアス）が用意されている。
• 通常アドレス + 0x2000 = アトミックセット（SET）専用アドレス
• 通常アドレス + 0x3000 = アトミッククリア（CLR）専用アドレス
hw_set_bits(reg, mask) を呼び出すと、
CPUはSET専用アドレスに対して1回の書き込み（Write）命令だけを発行する
（ReadやModifyは不要）。

　一旦済●README.md の作成 ＆ 実機動作のGIF/写真撮影
●回路構成、
     済：●苦労したバグと解決法、美咲フォントのライセンス、
        AI活用の明記をまとめたドキュメント作成。
     ✖：●74HC138のイネーブルピン（/E1, /E2, E3）による物理ブランキングの検討
        ⇒イネーブルピンは今回不使用とのこと。
    済●今回ソフト側（74HC595への 0xFF 出力）でゴースト対策をするが、
    「もし基板のイネーブルピンがマイコンに繋がっていたらGPIO制御でも消灯できる」
    という回路的知識をメモしておく。
    
●アロー演算子記法のルール整理（リスト内の記法補足）
リストにある led_matrix->bytes[].x0 について：
bytes[y] は単なる uint8_t（数値）の配列なので、
後ろに .x0 を付けることはできない。
ビットアクセスしたい場合は led_matrix->grid[y].x0 と書く
（一番外側のポインタ部分だけを -> に変え、内部の配列や構造体は . を使う）。

●一部関数内でに作成した基本操作関数を使わずに処理している部分が無いか精査
●reverse_bits関数が使われていない問題
●表示したい点灯パターンを8*8の1コマとして、複数コマ配列で保持してそれを点灯
●LEDバッファのポインタを作成
●マルチバッファ制御の実装
●ポインタ渡し
●アロー演算子でのアクセス「ポインタ渡し（コピーを避ける）」
led_matrix->bytes[].(⇐ここのドットもアローに出来る？) x0でのアクセス
●関数ポインタの作成と関数ポインタを渡しての処理の実行
●ISR割り込み処理の実装。割り込み処理に何をさせるか(LED点灯画面更新？)
●ISRに関数ポインタを渡して、コールバック処理の自由度を上げる。
●ステートパターン（モード切替）の実装。
◆通常のレジスタ操作では問題があるのはなぜか？
●「読む→変える→書く」の隙間
ex：reg = reg | BIT(3);
この1行は、CPUの中では3つの手順に分かれている。
１．reg を読む
２．読んだ値に BIT(3) を OR する（変える）
３．結果を reg に書き戻す
この方式を「リード・モディファイ・ライト（RMW）」と呼ぶ。
◆問題は、1と3の間に隙間があること。
例：割り込みで変更が消える
・メイン処理：reg を読む（値は 0b0000）
★ここで割り込み発生：割り込み処理が bit0 を1にする（reg = 0b0001）
・メイン処理に戻る：さっき読んだ 0b0000 に bit3 を足して 0b1000 を書き戻す
・結果：割り込みで立てた bit0 が消える
割込みの変更が、先に読み出しを済ませていた古いデータ（今回のメイン処理のメモ）によって消えてしまう現象。
◆解決方法：「1にしたいビットだけ伝える」専用の書き込み口
読まずに済めば、隙間はなくなる。そこでハードウェア側に、次のような働きをする書き込み口が用意されている。
これを 一般的な周辺機器（ベースレジスタ＋オフセット方式）:
通常のレジスタ空間では、アドレスにオフセットを加算することでリード・モディファイ・ライト（RMW）を避け、
1回の書き込みで操作できる。
RP2040には、この書き込み口を用意する方法が2種類ある。ここが元の説明の要点です。
●方式A：一般の周辺機器 ― 同じレジスタを4つの住所から見る
SPI、PWM、パッド設定など、ほとんどの周辺機器のレジスタがこの方式。
1つのレジスタが、4つのアドレスから見えるようになっている。
アドレス	書き込んだときの動作
・元のアドレス + 0x0000	普通に上書き
・元のアドレス + 0x1000	XOR（1のビットを反転）
・元のアドレス + 0x2000	SET（1のビットを1に）
・元のアドレス + 0x3000	CLR（1のビットを0に）
ロッカー1つに窓口が4つあるイメージです。どの窓口から入れても、中身は同じロッカー。
ただ、窓口ごとに「入れ方のルール」が違う。この仕組みを「エイリアス（別名アドレス）」と呼ぶ。
※hw_set_bits(&reg, mask) は、「reg のアドレス + 0x2000 に mask を書く」関数。
※hw_clear_bits は +0x3000、hw_xor_bits は +0x1000 に書く。
●方式B：SIO（GPIO）― 最初から別々のレジスタがある
GPIOの出力は SIO（Single-cycle I/O）という部分が担当。
SIOは、CPUコアに直接つながっている。そのおかげで、1クロックで読み書きできるほど速い。
その代わり、方式Aの「+0x2000の窓口」の仕組みは使えない。
エイリアスの仕組みは、普通のバスの経路にあるもので、SIOはその経路を通らない。
そこでSIOには、最初から役割ごとに別のレジスタが用意されている。
レジスタ	役割	使う関数
・GPIO_OUT	出力値そのもの（普通に上書き）	（直接書くことは少ない）
・GPIO_OUT_SET	1のビットのピンをHighに	gpio_set_mask(mask)
・GPIO_OUT_CLR	1のビットのピンをLowに	gpio_clr_mask(mask)
・GPIO_OUT_XOR	1のビットのピンを反転	gpio_xor_mask(mask)
gpio_put(pin, value) も、中では SET か CLR に1回書いているだけ。
◆2つの方式の比較
方式A（一般の周辺機器）	方式B（SIO / GPIO）
書き込み口の作り方	同じレジスタの別アドレス（+0x1000/2000/3000）	最初から別々のレジスタ
使う関数	hw_set_bits / hw_clear_bits / hw_xor_bits	gpio_set_mask / gpio_clr_mask / gpio_xor_mask
結果	書き込み1回で、他のビットを壊さない	同じ
元の説明の「どちらも1回の書き込みで済む点は同じ」は、この表の最後の行のこと。
◆方式A；実際のコード例（hardware/address_mapped.h）
#include "hardware/address_mapped.h" // 方式A用のマクロが入っているヘッダ
// 例：あるレジスタ（reg）の 第3ビット（0b0000_1000）だけを操作したい場合
// ① 第3ビットだけを 1 にする (SET : アドレス + 0x2000 に書き込む)
hw_set_bits(&reg, (1u << 3));
// ② 第3ビットだけを 0 にする (CLR : アドレス + 0x3000 に書き込む)
hw_clear_bits(&reg, (1u << 3));
// ③ 第3ビットだけを 反転 する (XOR : アドレス + 0x1000 に書き込む)
hw_xor_bits(&reg, (1u << 3));
※内部で起きていること。hw_set_bits(&reg, mask) を呼び出すと、関数の中では以下のような
計算が1行で実行されている。*(レジスタのアドレス + 0x2000) = mask;
◆方式B】SIO（GPIO操作）の書き方
〜「最初から用意されている SET / CLR レジスタに直接書く」〜
GPIO（ピンのON/OFF）はCPU直結の爆速バス（SIO）にあるため、上記の「+0x2000を足す窓口」が存在しない。
その代わり、最初から名前の違うレジスタが並んでいる。
:実際のコード例（hardware/gpio.h）
#include "hardware/gpio.h"
// 例：GP2 と GP3 を同時に操作したい場合（マスクを作成）
uint32_t mask = (1u << 2) | (1u << 3); // GP2とGP3の位置だけ1にした32ビット値
// ① GP2 と GP3 を同時に High にする (GPIO_OUT_SET レジスタへ書く)
gpio_set_mask(mask);
// ② GP2 と GP3 を同時に Low にする (GPIO_OUT_CLR レジスタへ書く)
gpio_clr_mask(mask);
// ③ GP2 と GP3 の High/Low を反転する (GPIO_OUT_XOR レジスタへ書く)
gpio_xor_mask(mask);
// ※gpio_put(2, 1) の内部処理イメージ
sio_hw->gpio_set = (1u << 2); // SETレジスタに直接1回書き込んでいるだけ。

●8*8*8LEDキューブでも対応可能なデータ構造を考える。
[[LEDBuffer_t]×8]の二重配列か？中のデータ型はLEDBuffer_tを使うとして、
外側のデータ型宣言、、、もLEDBuffer_t型で良いかな。
static LEDBuffer_t led_3d_matrix_buffer[8] = { .bytes = {0} };？
static を付けた変数は、自動的に0で初期化される。
なので、static LEDBuffer_t led_cube[8]; だけで十分。中身は実質 uint8_t[8][8] になる。
階層	表すもの	コード上の表現
最外（Z軸）	キューブの「高さ（レイヤー）」	led_cube[z] （0〜7の計8面）
中間（Y軸）	1面の中の「行（ライン）」	led_cube[z].bytes[y] （0〜7の計8行）
最内（X軸）	1行の中の「列（ビット）」	(led_cube[z].bytes[y] >> x) & 0x01 （8ビット）

 // @brief 8x8 LEDフレームバッファ（1面分）の共用体
 // typedef union {
//     uint8_t  bytes[8];    //< SPI送信・一括処理用の8バイト配列 
//     RowBits  grid[8];     //< (x, y) 座標ビットフィールド配列 
//     uint64_t all_bits;    //< 1面全体(64ビット)を一括クリア・コピーするための変数
// } LEDBuffer_t;
// これを8面分用意する
// static LEDBuffer_t led_cube[8];
// led_cube[z].grid[y].x3 = 1;  // 座標 (X:3, Y:y, Z:z) を点灯
// ●プログラムファイルの分割
// ・グローバル変数を減らす。
// ・内部的なデータおよび関数にstaticを付ける。
// ・inline化が望ましい関数にはinlineを付ける。
*/
#endif


/**
 * @file Sample_matrixLED03_refactored.c
 * @brief Raspberry Pi Pico 8x8 LEDマトリクス制御プログラム
 * @details 74HC595(列カソード/SPI)および74HC138(行アノード/GPIO)を
 * 用いたダイナミック点灯制御ライブラリ
 * @note 環境： Raspberry Pi Pico（RP2040）、Pico SDK 2.3.1、VS Code拡張機能、
 * CMake + Ninja、arm-none-eabi-gcc、カソード側 74HC595（SPI 1MHz）、
 * 行選択 74HC138（GPIO 18〜20）
 */
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"


// 美咲フォントヘッダのインクルード
#include "misaki_font.h"

// --- ピン割り当て定義 ---
#define PIN_MOSI 7   // 74HC595 SER (データ)
#define PIN_SCK  6   // 74HC595 SRCLK (クロック)
#define PIN_RCK  3   // 74HC595 RCLK (ラッチ)
#define DMUX_A   20   // 74HC138 A
#define DMUX_B   19   // 74HC138 B
#define DMUX_C   18   // 74HC138 C
#define SPI_PORT spi0

#define ON true
#define OFF false

// 合成モード定義
#define OVERLAY_MODE_OR   0 /**< OR合成 (重ね合わせ) */
#define OVERLAY_MODE_AND  1 /**< AND合成 (マスク・積集合) */
#define OVERLAY_MODE_XOR  2 /**< XOR合成 (反転重ね合わせ) */

// --- ビットシフト演算用定義
#define BIT(n) (1u << (n))
// 1を左側に n 回ビットシフトさせる。1*2^n と同じ意味。BIT(n)と書くだけでOKになる。
// BIT(0) ⇒(1u << 0)⇒0b0000_0001
// BIT(3) ⇒(1u << 3)⇒0b0000_1000
// ◆使用例
// v (value値 や variable変数の略でよく使われる)
// v |=  BIT(3);              // セット：第3ビットを1に
// v &= (uint8_t)~BIT(1);     // クリア 
// v ^=  BIT(n);              // 反転 フラグを反転などに（TOGGLE）操作。
// if (v & BIT(3)) { ... }    // 判定  // 第3ビットが1の場合の処理

#define USE_BIT_SHIFT 0   // 1にするとビット演算版に切り替わる

#define USE_UTF_MODE 0 //0でアスキーコード表示関数、1でUTFコード表示関数有効

// -------------------------------------------------------------
// タイマー・周波数に関する定数定義 (500Hz 設定)
// -------------------------------------------------------------
// ●1フレーム（8行）にかかる時間 
// 　1秒(1,000,000μs) ÷ 500Hz = 2,000μs ＝ 2ms
// ●1行あたりの切り替え時間（sleep_us の値）
// 　2,000μs ÷ 8行 = 250μs
#define ROW_SCAN_TIME_US 250 /**< 1行あたりの点灯時間 (250us) */

// デフォルト時間設定
#define DEFAULT_RENDER_DURATION_MS 10000 /**< デフォルト点灯保持時間 (10000ms = 10秒) */
#define DEFAULT_BLINK_INTERVAL_MS   300   /**< デフォルト点滅間隔 (300ms) */
#define DEFAULT_BLINK_TIMES        10    /**< デフォルト点滅回数 (10回) */
#define DEFAULT_SCROLL_STEP_MS     360    /**< デフォルト1ドット送り速度 (360ms) */

// テストコード切り替え用
#define TEST_MODE_595 1
#define TEST_MODE_138 0
#define TEST_MODE_ALL 0

// -------------------------------------------------------------
// データ構造定義（共用体 union）
// -------------------------------------------------------------

// 原点 (0, 0)：画面の左上隅。
// ■データ構造（共用体 union の活用）
/**
 * @brief 1行分(8ビット)のビットフィールド構造体
 */
typedef struct {
    uint8_t x0 : 1; /**< 0列目(左端) */
    uint8_t x1 : 1; /**< 1列目 */
    uint8_t x2 : 1; /**< 2列目 */
    uint8_t x3 : 1; /**< 3列目 */
    uint8_t x4 : 1; /**< 4列目 */
    uint8_t x5 : 1; /**< 5列目 */
    uint8_t x6 : 1; /**< 6列目 */
    uint8_t x7 : 1; /**< 7列目(右端) */
} RowBits;
// ex：row[y].x0 = 1; // y行x0（0ビット目）に 1（点灯）をセット

/**
 * @brief 8x8 LEDフレームバッファ共用体
 * @details 8バイト一括(bytes)および (x,y)ビットアクセス(grid)を同じメモリで共有
 */
typedef union {
    uint8_t bytes[8]; /**< SPI送信・一括処理用の8バイト配列 */
    RowBits grid[8]; /**< (x, y) 座標ビットフィールド配列。grid[y].x0 〜 grid.x7 で1ビットずつ直接アクセスする */
} LEDBuffer_t;
// マトリクスLED構造※左上が原点（型番印刷側面を下とした時）
//     左上 x 0     1    2    3    4    5    6    7
// bytes[y]0 [0].x0,[0].x1
// bytes[y]1 [1].x0,[1].x1...
// bytes[y]2
// bytes[y]3
// bytes[y]4
// bytes[y]5
// bytes[y]6
// bytes[y]7

/**
 * @brief グローバル・フレームバッファ（下書き用キャンバス）の宣言と初期化
 */
static LEDBuffer_t led_matrix_buffer = { .bytes = {0} };
// ■グローバルまたはモジュール内の基準バッファ
// ・{ .bytes = {0} }は指示付き初期化子（Designated Initializer）」という文法。
// 共用体は複数のメンバーを持っているので、普通に {0} と書くだけだと
// 「どのメンバーを初期化しているのか」が分からない。
// .bytes = {0} と書くことで、共用体の中の .bytes 配列メンバーを
// 指定して、全要素を 0（全消灯状態）で初期化の意味になる。

#if USE_UTF_MODE

/**
 * @brief スクロール表示用サンプルテキスト
 * @note ■ UTF-8 使う場合は ASCII版をコメントアウトしてこちらを使用
 * @typed wchar_t　※型が異なるので注意
 */
static const wchar_t scroll_text[] = L" Hello Pico! こんにちは！";
// ※表示させたいメッセージ（日本語・英数字ミックスOK）
// ●C言語コンパイラに「文字」は「数値（文字コード）」に変換されるのでそのまま文字列を入れる。
#else
/**
 * @brief スクロール表示用サンプルテキスト※表示させたい文字列を入れる配列
 * @note ■ ASCIIコード版
 * @typed char
 */
static const char scroll_text[] = " HELLO PICO! 12345 ";
#endif

// =============================================================
// 0. ハードウェア直接制御層 (HAL / 内部ヘルパー関数)
//    ※ GPIOやSPIを直接叩いてIC(74HC138/595)を物理操作する低レイヤー
// =============================================================

static inline uint8_t reverse_bits(uint8_t b);
static inline void hw_matrix_led_blanking(void);
static inline void hw_matrix_select_active_row(uint8_t row);       
static inline void hw_matrix_output_line(uint8_t row, uint8_t line_data); 

/**
 * @brief 8ビットデータのLSB/MSBビット順逆転関数
 * @param[in] b 対象の8ビットデータ
 * @return ビット順が前後反転した8ビットデータ
 */
static inline uint8_t reverse_bits(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4; // 4ビット単位（前半と後半）を入れ替え
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2; // 2ビットペアを入れ替え
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1; // 隣り合う1ビット同士を入れ替え
    return b;
}

/**
 * @brief 74HC595(カソード)へ全High(0xFF)を出力(low点灯high消灯)し、
 *　LEDを消灯(ブランキング)する
 * @note 行切り替え時のゴースト(残像)を防止するために使用する。
 *　※バッファクリア関数は描画用の下書きをクリアする関数なので役割が違う。
 */
static inline void hw_matrix_led_blanking(void){
    uint8_t clear_data = 0xFF;
    spi_write_blocking(SPI_PORT, &clear_data, 1);
     // ラッチパルス(RCK)で出力を更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);
}

/**
 * @brief 74HC138 (デコーダ/アノード行選択※点灯行選択) 出力設定関数
 * @param[in] row 選択する行番号 (0 〜 7)
 */
static inline void hw_matrix_select_active_row(uint8_t row)
// ●「uint8_t row」<stdint.h>で定義されている「符号無し8ビット整数(0～255)」。
// unsigned char型と同じサイズだが、8bit(1byte)サイズであることを「明示」する
// 為にこちらを使う。「row」は点灯させたい行を受け取る変数。
{
	if (abs(row) >= 7) retrun;
	
    gpio_put(DMUX_A, (row >> 0) & 0x01); // 0ビット目
    gpio_put(DMUX_B, (row >> 1) & 0x01); // 1ビット目
    gpio_put(DMUX_C, (row >> 2) & 0x01); // 2ビット目
    // ●74HC138との連携：74HC138は、3本のバイナリ入力端子(A,B,C)の「H/L」の
    // 組み合わせで8本の出力端子(Y0~Y7)のどれか「１つ」に「Low」を出力する、
    // 3to8ラインデコーダ機能を持ったIC。デマルチプレクサの1種。
}

/**
 * @brief 1行分の物理信号出力関数（カソードデータSPI送信 ＋ アノード行有効化）
 * @param[in] row 出力対象の物理行番号 (0 〜 7)
 * @param[in] line_data 表示させる1行分(8ドット)の点灯パターン (1=ON, 0=OFF)
 * @note カソード制御のため、内部でビット反転(~)を行って送信する。
 */
static inline void hw_matrix_output_line(uint8_t row, uint8_t line_data)
{   
    // ●uint8_t row: 表示対象の行番号（0 〜 7）。
    // uint8_t line_data: その行に表示させたい 1行分（8ドット）のLED点灯パターン。
    // 元データでは、1 ＝ 点灯（ON）、0 ＝ 消灯（OFF）という直感的なデータになっている。

    // 先に74HC138でアノード行を選択（点灯はまだ）
    hw_matrix_select_active_row(row);
    // ●行選択（アノード側の切り替え）

    // ●カソード制御のためビット反転(~)
    // 74HC595はLEDカソード側を制御している＝LEDを点灯させるには、
    // カソード側をLowにする必要がある。その為、1=点灯になっている元データを
    // ビット反転演算子で全ビットを反転させる。
    uint8_t inv_data = ~line_data;
    spi_write_blocking(SPI_PORT, &inv_data, 1);
    // ●int spi_write_blocking
        // (spi_inst_t *spi, const uint8_t *src, size_t len)。
    // ●len (Length): src のアドレスから「何バイト分を順番に送るか」
    // というデータ長指定。
    // 今回は 1 バイト（8ビット）送るため 1 を指定。
    // もし 10 バイトの配列をまとめて送りたければ 10 を指定。

    // ラッチパルス(RCK)で出力を更新
    gpio_put(PIN_RCK, 1);
    sleep_us(1);
    gpio_put(PIN_RCK, 0);
}

/**
 * @brief ハードウェア初期化処理
 */
void init_hardware(void) {
    stdio_init_all();

    // SPI初期化 (100kHz⇒1000kHzに変更)
    spi_init(SPI_PORT, 1000 * 1000);
    // ブランキングで1バイト送るたびに、SPIが100kHzだと約80µsかかりる。
    // （1MHz）に上げると約8µsで済む。74HC595はこの速さでも問題なく動く。

    // SPI通信(74HC595)用ピン機能設定
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI); // 74HC595 SER (データ)※データ入力
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI); // 74HC595 SRCLK (クロック)※データシフト
    gpio_set_function(PIN_RCK,  GPIO_FUNC_SIO); // 74HC595 RCK (ラッチ)※データ確定

    // 74HC138 DMUXピン機能設定
    gpio_set_function(DMUX_A,    GPIO_FUNC_SIO); // 74HC138 A
    gpio_set_function(DMUX_B,    GPIO_FUNC_SIO); // 74HC138 B
    gpio_set_function(DMUX_C,    GPIO_FUNC_SIO); // 74HC138 C

    // ピン方向を出力に設定
    gpio_set_dir(PIN_RCK, GPIO_OUT);
    gpio_set_dir(DMUX_A,   GPIO_OUT);
    gpio_set_dir(DMUX_B,   GPIO_OUT);
    gpio_set_dir(DMUX_C,   GPIO_OUT);

    gpio_put(PIN_RCK, 0);
}

// -------------------------------------------------------------
// １．バッファ操作（ゲッター・セッター、全画操作など）モジュール
// 　　※下書きキャンバス(led_matrix_buffer)」のみを変更するメモリ操作
// -------------------------------------------------------------

void led_buffer_set_pixel(uint8_t x, uint8_t y, bool on_off);
bool led_buffer_get_pixel(uint8_t x, uint8_t y);
void led_buffer_clear_all(void);

/**
 * @brief 1ピクセル(x, y)の点灯(ON)/消灯(OFF)をバッファにセットする関数
 * @param[in] x X座標 (0 〜 7)
 * @param[in] y Y座標 (0 〜 7)
 * @param[in] on_off ON(true) または OFF(false)
 */
void led_buffer_set_pixel(uint8_t x, uint8_t y, bool on_off){
    // 範囲外ガード（0〜7以外の値が入ってきたら何もしない）
    if (y >= 8 || x >= 8) return;
    #if USE_BIT_SHIFT
    // ◆ビットシフト版 #define USE_BIT_SHIFT 0  を 1 にするとビット演算版に切り替わる。
    // ※両者が同じ結果になるのは、x0 が bit0 に割り付けられる環境
    // （GCC + ARM / AAPCS）のため。
    if(on_off){ //ONが渡されたら点灯
        led_matrix_buffer.bytes[y] |= BIT(x);
    }else{ //OFFが渡されたら消灯
        led_matrix_buffer.bytes[y] &= (uint8_t)~BIT(x);
    }
    #else
    switch (x) {
    // switch文だと直感的に操作が出来るが、標準的なのはビット演算。
    // ビットフィールドメンバは配列にできない
    // （grid[y].x[n] のように添字で指定できない）為、
    // x の値（0〜7）に応じて、対応するビットフィールドメンバに直接代入
        case 0: led_matrix_buffer.grid[y].x0 = on_off; break;
        case 1: led_matrix_buffer.grid[y].x1 = on_off; break;
        case 2: led_matrix_buffer.grid[y].x2 = on_off; break;
        case 3: led_matrix_buffer.grid[y].x3 = on_off; break;
        case 4: led_matrix_buffer.grid[y].x4 = on_off; break;
        case 5: led_matrix_buffer.grid[y].x5 = on_off; break;
        case 6: led_matrix_buffer.grid[y].x6 = on_off; break;
        case 7: led_matrix_buffer.grid[y].x7 = on_off; break;
    }
    #endif
}

/**
 * @brief 指定座標(x, y)のピクセル状態を取得
 * @param[in] x X座標 (0 〜 7)
 * @param[in] y Y座標 (0 〜 7)
 * @return 点灯していれば true, 消灯または範囲外なら false
 */
bool led_buffer_get_pixel(uint8_t x, uint8_t y){
    if (y >= 8 || x >= 8) return false;
    // 戻り値の型が boolのため、早期リターンも何かboolを返す必要がある。
    // x と y が uint8_t なので、誰かが −1 を渡しても 255 に変換
    // される。>= 8 のチェックで、それも弾ける。意図していなかった汗。
    #if USE_BIT_SHIFT
    // ◆ビットシフト版　#define USE_BIT_SHIFT 0  を 1 にするとビット演算版に切り替わる。
        return ((led_matrix_buffer.bytes[y] >> x) & 0x01) ;
    // ex:x = 4,bytes[y]=0b0001_0000とする。
    // (0b0001_0000 >> x) & 0x01 ※xビット分シフトして、
    // 0x01と論理積して狙いのピクセルのビットを取り出して、
    // return で返す。& 0x01 で他の桁を消しているので01しか返らない＝true,falseのみ
    #else
    switch (x) {
        case 0: return led_matrix_buffer.grid[y].x0;
        case 1: return led_matrix_buffer.grid[y].x1;
        case 2: return led_matrix_buffer.grid[y].x2;
        case 3: return led_matrix_buffer.grid[y].x3;
        case 4: return led_matrix_buffer.grid[y].x4;
        case 5: return led_matrix_buffer.grid[y].x5;
        case 6: return led_matrix_buffer.grid[y].x6;
        case 7: return led_matrix_buffer.grid[y].x7;
        // stdbool.h の設定で関数の戻り値型が bool の場合、
        // 0 は false、1（非ゼロ）は true に自動的に変換される。
        // x0（1ビット）しか入っていないので自動的にtrue/falseになる。
    }
    return false; // ここに来ることはないが、コンパイラ警告回避のために追加
    #endif
}


/**
 * @brief フレームバッファの全消灯（クリア）
 */
void led_buffer_clear_all(void){
     for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = 0x00; //0b0000 0000
        // 共有体の8ビットごと扱えるbytes[]でアクセスして全部0に。
    }
}

/**
 * @brief フレームバッファの全点灯（全ピクセルON）
 */
void led_buffer_fill_all(void){
     for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = 0xFF; // 0b1111 1111
    }
}

/**
 * @brief フレームバッファの全画面白黒（点灯/消灯）反転
 */
void led_buffer_invert_all(void){
     for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = ~led_matrix_buffer.bytes[i]; 
        // 全ビット反転
    }
}

// -------------------------------------------------------------
// ２．行・列（ライン）単位のバッファ操作
// -------------------------------------------------------------
void led_buffer_set_row(uint8_t y, uint8_t row_data); //指定した1行に8ビットデータを書き込み
uint8_t led_buffer_get_row(uint8_t y); //指定した1行の8ビットデータを取得
void led_buffer_set_col(uint8_t x, uint8_t col_data);  //指定した1列に8ビットデータを縦方向に書き込み
uint8_t led_buffer_get_col(uint8_t x); //指定した1列の8ビットデータを取得

/**
 * @brief 指定した1行(y行目)に8ビットデータを一括書き込み
 * @param[in] y 行番号 (0 〜 7)
 * @param[in] row_data 1行分の8ビットパターン ex0b1010_1111
 */
void led_buffer_set_row(uint8_t y, uint8_t row_data) {
    if (y >= 8) return;
    led_matrix_buffer.bytes[y] = row_data;
}

/**
 * @brief 指定した1行(y行目)の8ビットデータを取得
 * @param[in] y 行番号 (0 〜 7)
 * @return 1行分の8ビットデータ
 */
uint8_t led_buffer_get_row(uint8_t y) {
    if (y >= 8) return 0;
    return led_matrix_buffer.bytes[y];
}

/**
 * @brief 指定した1列(x列目)に縦方向8ビットデータを一括書き込み
 * @param[in] x 列番号 (0 〜 7)
 * @param[in] col_data 縦1列分の8ビットパターン (bit0=y0x0... bit7=y7x0)
 */
void led_buffer_set_col(uint8_t x, uint8_t col_data) {
    if (x >= 8) return;
    for (uint8_t y = 0; y < 8; y++) {
        bool state = (col_data >> y) & 0x01;
        led_buffer_set_pixel(x, y, state);
        // ex:x=3,coldate=0b0110_1010の時
        // y=3ループ目、3bit⇒シフトして0b0000_1101、0b0000_0001と論理積
        // ⇒yビット目だけの状態(0 or 1)を取り出せる。
        // それをLEDバッファの1ピクセルにセットする。
        // これを縦一列分続ける。
    }
}

/**
 * @brief 指定した1列(x列目)の縦方向8ビットデータを取得
 * @param[in] x 列番号 (0 〜 7)
 * @return 縦1列分の8ビットデータ
 */
uint8_t led_buffer_get_col(uint8_t x) {
    if (x >= 8) return 0;
    uint8_t col_data = 0;
    for (uint8_t y = 0; y < 8; y++) {
        if (led_buffer_get_pixel(x, y)) {
            // LEDブッファの1ピクセルの状態が点灯ならif内処理
            col_data |= (1 << y);
            // y0,y1,y2=offで非実行でy3=onなら、
            // ビット論理和代入演算子でcol_data自身と、y=3なので、
            // 1＝0001を3bit(y分)ずらして1000=8。これとcol_dateの
            // 論理和0b0000_0000,0b0000_1000で0b0000_1000がcoldateにセット
            // これを縦一列分繰り返して返す。
        }
    }
    return col_data;
}

// -------------------------------------------------------------
// ３．シフト（移動）バッファ操作
// -------------------------------------------------------------
void led_buffer_shift_cols(int offset_times); 
// 画面全体を左右に offset_times ピクセル分シフト（正＝右、負＝左）
void led_buffer_shift_rows(int offset_times); 
// 画面全体を上下に offset_times ピクセル分シフト（正＝上、負＝下）

// shift_colsと機能重複している？⇒いや下の2関数は新しい描画データを挿入している。
// ⇒名前の変更が必要だ。
void led_buffer_push_col_from_right(uint8_t new_rightend_col_data);
void led_buffer_push_col_from_left(uint8_t new_leftend_col_data);

// 【画面上の配置】   [x0:左端]  [x1]  [x2]  [x3]  [x4]  [x5]  [x6]  [x7:右端]
//                     │       │     │     │     │     │     │       │
// 【C言語のビット】  bit0    bit1　bit2  bit3  bit4  bit5  bit6  bit7 (MSB)
//                   (LSB/左)                                     (MSB/右)
//  0b1 0 1 0 1 0 1 0 実際のビットの並びとLED画面への反映が反転しているので注意。
//bit 7|6|5|4|3|2|1|0 番目
/**
 * @brief 画面全体を左右に offset_times ピクセル分シフト（正＝右へ、負＝左へ※左上原点）
 * @param[in] offset_times シフトドット数 (正の値で右シフト、負の値で左シフト)
 */
void led_buffer_shift_cols(int offset_times) {
    if (offset_times == 0) return;
    if(abs(offset_times) >= 8){ led_buffer_clear_all(); return;}

    if (offset_times > 0) { // 右シフト (x7方向へ詰める)
        for (uint8_t y = 0; y < 8; y++) {
            led_matrix_buffer.bytes[y] 
            = led_matrix_buffer.bytes[y] << offset_times;
            // ex:offset_times=2、元行データ0b0110_1001なら、
            // 0b⇐旧01|1010_01|新00⇐ これを8行分繰り返すと縦1列が横にシフトした様に見える。
            // ※<<2 は「右端（bit7, bit6）の2列が落ちて、左端（x0, x1）に空き列が入る。
        }
    } else { // 左シフト (x0方向へ詰める)
        int abs_offset_times = -offset_times;
        for (uint8_t y = 0; y < 8; y++) {
            led_matrix_buffer.bytes[y] 
            = led_matrix_buffer.bytes[y] >> abs_offset_times;
            // ex:offset_times=-2、元行データ0b0110_1001なら、
            // 0b⇒新00|01_10_10|旧01⇒ これを8行分繰り返すと縦1列が横にシフトした様に見える。
        }
    }
}
// ◆32bit以上のデータを渡すと未定義動作になる理由。
// 32ビットのデータに対して32以上シフトすると、CPUの仕様によって
// 『全消灯』するどころか『1ビットもシフトされずに元データがそのまま残る』という
// 事故が起きる為。
// 1. 理由：C言語のルール（整数拡張）
// led_matrix_buffer.bytes[y] は uint8_t（8ビット）型だが、
// 「整数拡張（Integer Promotion）」のルールにより、
// C言語がシフト演算を行うときは自動的に int型（32ビット）に拡張されて計算される。
// C言語の国際規格（ISO C）では、以下のようなルールが定められている。
// 「型のビット幅（今回は32ビット）以上の回数をシフトした場合、
// その結果は未定義（Undefined Behavior）とする」
// つまり、「32ビットの箱に入っているデータを32回以上動かすな
// （何が起きても保証しない）」というルールになっている。
// 2. 理由：CPU（ARM / RP2040）の物理的な仕組み
// これが一番の理由。
// Raspberry Pi PicoのCPU（ARM Cortex-M0+）などの一般的なマイコンは、
// シフト命令を実行する際、処理速度を極限まで速くするために
// 「シフト回数の下位5ビット（0〜31）しか見ない（＝32で割った余りを使う）」
// というハードウェア構造になっている。
// もし offset_times = 32 を渡してしまうと、
// 内部では次のような計算になる。
// 人間の期待: 「32回シフトしたから、データが 0 になるはず」
// CPUの実際の動き: 32 % 32 = 0 （32で割った余りは0）と解釈され、
// 「0回シフト（＝1ビットも動かさず元データをそのまま残す）」
// という真逆の動作をしてしまう。
// 同様に 33 回シフトしようとすると、33 % 32 = 1 
// で「1回シフト」と同じ結果になってしまう。


/**
 * @brief 画面全体を上下に offset_times ピクセル分シフト（正＝上、負＝下）
 * @param[in] offset_times シフトドット数 (正の値で上シフト、負の値で下シフト)
 */
void led_buffer_shift_rows(int offset_times) {
    if (offset_times == 0) return;
    if(abs(offset_times) >= 8){ led_buffer_clear_all(); return;}

    if (offset_times > 0) { // 上シフト (y=0方向へ移動、下端に0を補填)
        for (int y = 0; y < 8; y++) {
            if (y + offset_times < 8) {
                // y方向(上)に3行シフトなら0+3=3。4,5,6,7の5回まで移動
                led_matrix_buffer.bytes[y] 
                = led_matrix_buffer.bytes[y + offset_times];
                // 8bit横一列一括で扱う。0行目(最上が0,最下が7)
                // 0行目を3行目で上書き。次ループ1行目に4行目を上書き。
            } else {
                // y+offset_times=8,9,10の残り下3行は全て0を詰める。
                led_matrix_buffer.bytes[y] = 0x00;
            }
        }
    } else { // 下シフト (y=7方向へ移動、上端に0を補填)
        int abs_offset_times = -offset_times;
        // 引数で受け取った負の値にマイナスを掛けてプラスにして絶対値に変換
        for (int y = 7; y >= 0; y--) {
            // 最大値のy=7から始め、yをデクリメントして行く。
            if (y - abs_offset_times >= 0) {
                // 入力が-3(下に3列シフト移動)なら、7-3=4>=0。
                led_matrix_buffer.bytes[y] 
                = led_matrix_buffer.bytes[y - abs_offset_times];
                // 上から7列目を4列目データで上書き、
                // 次ループ6列目を3列目データで上書き、5,2、4,1、
                // 最後3列目データを0列目(最上列)データで上書き
            } else {
                led_matrix_buffer.bytes[y] = 0x00;
                // そのあとは残った上の列2,1,0は0で埋める。
            }
        }
    }
}


/**
 * @brief 画面バッファ全体を1ピクセル左⇐へ送り出し、右⇒端列に新しい1列データを追加
 * @param[in] new_rightend_col_data 画面右端(7bit目)に追加挿入する縦1列分(8bit ex:0b1111_0000)のデータ
 */
void led_buffer_push_col_from_right(uint8_t new_rightend_col_data) {
    for (uint8_t y = 0; y < 8; y++) {
        uint8_t new_bit = (new_rightend_col_data >> y) & 0x01;
        // 次の右端列データが0b1101_0011で5ループ目なら、
        // (new_rightend_col_data >> y) & 0x01
        // ⇒5シフトして0b0000_1101これと0b0000_0001の論理積で、
        // 5ビット目だけを取りして、new_bitにセット⇒0b0000_0001
        // 右端に追加するのでx7の位置まで左シフトして0b1000_0000
        led_matrix_buffer.bytes[y] 
        = (led_matrix_buffer.bytes[y] >> 1) | (new_bit <<7);
        // 5行目の8bitデータex:0b0111_1010なら画面左へ動かすならx0方向にシフトする
        // ので、⇒1シフトして0b新0|011_1101|旧0、newbit0b1000_0000これと論理和して
        // 0b1011_1101これで5行目の8bit全データを上書きして1ビットずれた様に。
        // これを8行分繰り返す。
    }
}

/**
 * @brief 画面バッファ全体を1ピクセル右⇒へ送り出し、左⇐端列に新しい1列データを追加
 * @param[in] new_leftend_col_data 左端(0bit目)に追加挿入する縦1列分(8bit ex:0b1111_0000)のデータ
 */
void led_buffer_push_col_from_left(uint8_t new_leftend_col_data) {
    for (uint8_t y = 0; y < 8; y++) {
        uint8_t new_bit = (new_leftend_col_data >> y) & 0x01;
        // 次の左端列データが0b1101_1011で5ループ目なら、
        // (new_leftend_col_data >> y) & 0x01
        // ⇒5シフトして0b0000_1101これと0b0000_0001の論理積で、
        // 5ビット目だけを取りして、new_bitにセット⇒0b0000_0001
        // 左端に追加するのでx0の位置なのでシフトせずこのまま次の計算に使用。
        led_matrix_buffer.bytes[y] 
        = (led_matrix_buffer.bytes[y] << 1) | new_bit;
        // 5行目の8bitデータex:0b0111_1010⇐1シフトして0b0|1111_010|0にこれと、
        // newbit0b0000_0001との論理和で、
        // 0b1111_0101これで5行目の8bit全データを上書きして1ビットずれた様に。
        // これを8行分繰り返す。
    }
}

// -------------------------------------------------------------
//  ４．演出・重ね合わせ（合成）バッファ操作
// -------------------------------------------------------------
void led_buffer_draw_pattern(const uint8_t pattern[8]);
// ：指定した8×8ドット絵パターンを一括転写
void led_buffer_overlay_pattern(const uint8_t pattern[8], uint8_t mode);
// ：OR合成 / AND合成 / XOR合成でパターンを重ね合わせ（キャラと背景の衝突や重なり表現に便利！）
void led_buffer_rotate_90(void); //画面全体を90度時計回りに回転 

/**
 * @brief 指定した8x8ドット絵パターン(8バイト配列)を一括転写
 * @param[in] pattern 8バイトのパターン配列
 */
void led_buffer_draw_pattern(const uint8_t pattern[8]) {
    if (pattern == NULL) return;
    for (int i = 0; i < 8; i++) {
        led_matrix_buffer.bytes[i] = pattern[i];
    }
}

/**
 * @brief 指定した8x8ドット絵パターンを合成モード(OR/AND/XOR)で重ね合わせ
 * @param[in] pattern 8バイトのパターン配列
 * @param[in] mode 合成モード (OVERLAY_MODE_OR, OVERLAY_MODE_AND, OVERLAY_MODE_XOR)
 */
void led_buffer_overlay_pattern(const uint8_t pattern[8], uint8_t mode) {
    if (pattern == NULL) return;

    for (int i = 0; i < 8; i++) {
        switch (mode) {
            case OVERLAY_MODE_OR:
                led_matrix_buffer.bytes[i] |= pattern[i];
                break;
            case OVERLAY_MODE_AND:
                led_matrix_buffer.bytes[i] &= pattern[i];
                break;
            case OVERLAY_MODE_XOR:
                led_matrix_buffer.bytes[i] ^= pattern[i];
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 画面全体を90度時計回りに回転
 */
void led_buffer_rotate_90(void) {
    LEDBuffer_t temp = { .bytes = {0} };

    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            // (x, y) ピクセルを取得し、回転先 (7-y, x) へ設定
            bool bit = (led_matrix_buffer.bytes[y] >> x) & 0x01;
            if (bit) {
                temp.bytes[x] |= (1 << (7 - y));
            }
        }
    }
    led_matrix_buffer = temp;
}

// -------------------------------------------------------------
// ５．点灯・タイミング制御などレンダラー（描画）関数群
// ※ バッファの内容を500Hzのダイナミック点灯で物理LEDへ出力表示する
// -------------------------------------------------------------

void led_render_blink(uint32_t interval_ms, uint8_t times);
void led_render_scan_once(void); 
void led_render_frame(uint32_t duration_ms); 
void led_render_scroll_text(const char *text, uint32_t step_delay_ms);
void mode_ticker_board(const char *text);

/**
 * @brief 現在のバッファ状態を指定ミリ秒間だけ静止・連続点灯表示する関数
 *（最小単位：8行分を1周だけ走査してダイナミック点灯スキャンする）
 * つまり「1画面描画（ダイナミック点灯）」の基本関数
 * @note  1回の呼び出しで 250μs × 8行 ＝ 約2ms かかる（500Hzスキャン）
 */
void led_render_scan_once(void) {
    for (uint8_t row = 0; row < 8; row++) {
        // 共用体バッファから最新の y=row 行目の8ビットデータ
        // （0b...）を直接参照
        uint8_t line_data = led_matrix_buffer.bytes[row];
        // 既存の1行描画関数を呼び出す（7 - row で上下反転も吸収）
        // ✖ hw_matrix_output_line(7 - row, line_data);
        // 修正: 7 - row をやめ、row を直接渡すことで上下反転バグを解消
        hw_matrix_output_line(row, line_data);
        // 1行あたりの点灯保持時間（2000μs/8行 = 250us）
        sleep_us(ROW_SCAN_TIME_US);
        // 次の行へ移る直前にブランキング（全消灯）
        hw_matrix_led_blanking();
    }
}

/**
 * @brief 現在のバッファ状態を指定時間の（duration_ms）間だけ静止・連続点灯表示する関数。
 * （現在のバッファ状態をそのまま点灯する）
 * @param[in] duration_ms 点灯保持時間（ミリ秒）。0指定時はデフォルト10000ms(10秒)
 * @note 桁あふれ（オーバーフロー）しないように、uint32_t型を渡する。
 */
void led_render_frame(uint32_t duration_ms) {
    if (duration_ms == 0) duration_ms = DEFAULT_RENDER_DURATION_MS;
    // 🔹 render（レンダリング / 描画）とは？
    // コンピュータグラフィックやマイコン制御における render は、
    // 「メモリ上のデータ（バッファ）をもとに、実際に画面やLEDへ出力・
    // 描画する処理」 のこと。 つまり led_render... は
    // 「バッファに描いた下書きを実際のLEDへ映し出す関数」という
    // 意味。
    // 🔹 frame（フレーム）とは？
    // パラパラマンガや動画の 「1コマの静止画」 のこと。
    // LEDマトリクス制御では、「ある一瞬に8×8のLED全体で
    // 表示されている1枚の点灯パターン」 を1フレームと呼ぶ。
     // 現在時刻から duration_ms ミリ秒後の「絶対終了時刻」を生成（Pico SDK標準関数）
    // 内部でget_absolute_time() で取得した現在時刻に引数で渡したミリ秒数（ms）を加算して返す。
    absolute_time_t end_time = make_timeout_time_ms(duration_ms);
    
    //没 uint32_t elapsed_ms = 0;
    // 1回の scan_once に約2msかかるため、
    // 経過時間を計算しながらループする
    // 🔹 なぜ duration_ms（表示時間）は uint32_t なのか？
    // duration ＝ 持続時間 / 表示する時間
    // ms ＝ ミリ秒（1/1000秒）
    // ミリ秒単位で「1秒（1000ms）」や「10秒（10000ms）」、
    // あるいは「1分（60000ms）」といった大きめの時間を指定したときに
    // 桁あふれ（オーバーフロー）しないようにするため。
    // uint8_t（最大255＝約0.25秒までしか測れない）では不十分なため
    // 時間を扱う引数には uint32_t（最大約49日分までOK）を使うのが
    // 標準的なルール。
    
    // 終了時刻に達するまでスキャンを繰り返す（time_reached関数で判定）
    // • static bool time_reached(absolute_time_t t)
    // • 引数: t（指定する絶対時刻／64bitタイマカウント値。1カウント＝1マイクロ秒）
    // • 戻り値: bool（指定した時刻に到達していれば true、まだであれば false）
    while (!time_reached(end_time)) {
        led_render_scan_once();
    }
}

/**
 * @brief 指定した間隔・回数で画面バッファの内容を点滅させる関数
 * @param[in] interval_ms 点滅間隔（ミリ秒）。0指定時はデフォルト(300ms)
 * @param[in] times 点滅回数。0指定時はデフォルト(10回)
 */
void led_render_blink(uint32_t interval_ms, uint8_t times) {
    // 0 が指定された場合はデフォルト値（定数）を採用する
    if (interval_ms == 0) interval_ms = DEFAULT_BLINK_INTERVAL_MS;
    if (times == 0)       times = DEFAULT_BLINK_TIMES;
    // 現在のバッファ状態を一時退避するための退避用バッファ
    LEDBuffer_t backup = led_matrix_buffer;
    for (uint8_t i = 0; i < times; i++) {
        // 消灯状態を表示
        led_buffer_clear_all();
        led_render_frame(interval_ms);
        // 元の絵を復元して表示
        led_matrix_buffer = backup;
        led_render_frame(interval_ms);
    }
}

// 文字のドットパターンについて確認
//{ L'\u0041', { 0x40, 0xA0, 0xA0, 0xA0, 0xE0, 0xA0, 0xA0, 0x00 } }, // 'A'
// 0b 0100 0|000
// 0b 1010 0|000
// 0b 1010 0|000
// 0b 1010 0|000
// 0b 1110 0|000
// 0b 1010 0|000
// 0b 1010 0|000
// 0b 0000 0|000
// ⇒5文字目まででカットしてOK。右端が隣の文字との空隙になる。
// { L'\u3093', { 0x10, 0x10, 0x20, 0x30, 0x4A, 0x4A, 0x84, 0x00 } }, // 'ん'
// 0b 0001 0000
// 0b 0001 0000
// 0b 0010 0000
// 0b 0011 0000
// 0b 0100 1010
// 0b 0100 1010
// 0b 0000 0000
/**
 * @brief 指定した文字列を1ドットずつスムーズに左スクロール表示する関数
 * @param[in] text スクロール表示させる文字列(ASCII)
 * @param[in] step_delay_ms 1ピクセル移動ごとの静止保持時間 (ミリ秒)
 */
void led_render_scroll_text(const char *text, uint32_t step_delay_ms) {
    if (text == NULL) return;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        // フォントデータ(8バイト)の取得
        const uint8_t *font_data = get_font_data_ascii(text[i]);
        // data[i] は i番目の要素の値。アドレスは &data[i]（または data + i）になる。
        // return font_table[i].data;今回戻って来るデータは、data[]配列のdataと言う配列名
        // の形になっている。配列名だけの時は内部的に&data[0]という配列の0番目の要素への
        // アドレスとして扱われる。なのでそれに間接参照*を付けた*font_dataはdata配列の
        // 最初の要素の8bitデータへのアクセスになる。
        // ⚠NULL != 0x00。NULLは「0」ではない。ポインタがどこも指していないという意味。
        // ポインタがNULLの時、関数がNULLを返すので、その際に
        // 文字がないNULL処理分の1ループをcontinueでスキップする。
        if (font_data == NULL) continue;
        int maxwidth = ((unsigned char)text[i] < 0x80) ? 5 : 8;
        // char を符号付きとして扱うコンパイラでは、0x80以上の値が負の数に。
        // そうなると < 0x80 が常に真になってしまうので、unsigned char型にキャストして、
        // 判定に利用する。※上は三項演算子の記述の仕方。
        // 0x80(128)未満は半角になるので、縦5列までにする
        // ※「実機で見て隙間2列が読みやすかったため」
        // 'A'の時 u0041
        // 1文字(5列)分、縦1列ずつ取り出してバッファに挿入＆レンダリング
        for (int col = 0; col < maxwidth; col++) {
            uint8_t col_pattern = 0;
            // ↓文字フォントから1列分(縦方向8ビット)をループで各行から取り出す。
            for (int row = 0; row < 8; row++) {
                // (7 - col) でフォントの左端(bit7)から抽出
                uint8_t bit = (font_data[row] >> (7 - col)) & 0x01;
                // bit7~bit3までが必要なので、(7 - col)のままで良い。
                // col=1,row=0,font_data[0]==0b 0100 0000の時
                // 7-1=6右シフト0b0000_0001 & 0x01で6ビット目を取り出せた。
                col_pattern |= (bit << row);
                // これを、row=0行目なので0シフト(そのまま)し、初期値0x00と論理和
                // colpattern==0x01が格納。これを縦1列データ分繰り返す。
            }
            // バッファを1ピクセル左シフトし、新しい1列を追加
            led_buffer_push_col_from_right(col_pattern);
            // 指定時間ぶんダイナミック点灯描画
            led_render_frame(step_delay_ms);
        }
    }    
}

#if 0   // ◆検討中：プロポーショナル幅計算（未完成のため無効化）

// 別の方法：プロポーショナル　文字ごとに実際の幅を計算する。
// これをどこに入れるか、if(text[i] < 0x80){
            // 'A'の時 u0041
            // 1文字(5列)分、縦1列ずつ取り出してバッファに挿入＆レンダリング
            // for (int col = 0; col < 5; col++) {
            // この処理の前に入れて、出たcolを5の位置に入れれば良いか。
for (size_t i = 0; i < len; i++) {
    // フォントデータ(8バイト)の取得
    // return font_table[i].data;
//     typedef struct {
//     wchar_t code;      // Unicode文字コード
//     uint8_t data[8];   // 8x8ドットデータ
// } FontItem;
    uint8_t maxwidth = 1;
    uint8_t char_widht = 0;
    const uint8_t *font_data = get_font_data_ascii(text[i]);
    for (int j = 0; j < 8; j++){
        char_widht |= font_data[j];
        // *font_data++;ポインタよりも配列でアクセス出来るときはした方が、
        // 可読性もバグも減る。
        // return font_table[i].data;が入っているので、
        // pointerをインクリメントすれば次のdata配列に行くはず。
    }
    for(int k = 0; k < 8; k++){
        if(((char_widht >> k) & 0x01) == 1) maxcol += 1;
        // if(k==7) col += 1;
        // 最初に1行空白確保していれば、後ろで空白列足す操作はいらない？
    }
    if(maxwidth < 4) maxwidth += ((7-maxwidth)/2) ;
    // maxwidth列数が3以下なら、空白なら(7-0)/2=3+1=4,
    // !なら((7-1)/2)=3+1=4
}

#endif //◆検討中：プロポーショナル幅計算

/**
 * @brief 電光掲示板モード（メッセージを常時無限ループスクロール表示）
 * @param[in] text 表示メッセージ文字列
 */
void mode_ticker_board(const char *text) {
    while (true) {
        led_render_scroll_text(text, DEFAULT_SCROLL_STEP_MS);
    }
}

// =============================================================
// 個別単体テストモジュール関数群
// =============================================================

/**
 * @brief 1ピクセル操作のテスト
 */
void test_pixel_control(void) {
    led_buffer_clear_all();
    led_buffer_set_pixel(0, 0, ON); // 左上
    led_buffer_set_pixel(7, 7, ON); // 右下
    led_render_frame(2000);
}

/**
 * @brief 1マスずつ順番に打点していくアニメーションテスト(累積)
 */
void test_accumulate_dots_animation(void) {
    led_buffer_clear_all();
    // バファクリアはループ前に一度だけ。
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            led_buffer_set_pixel(x, y, ON);
            led_render_frame(50); // 50msごとに1ドット増える
        }
    }
}

/**
 * @brief 単一1ドット掃引テスト（前のドットは消えて1つのドットが走る）
 */
void test_one_by_one_dot_animation(void) {
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            led_buffer_clear_all();
            // ループの度にバファをクリア
            led_buffer_set_pixel(x, y, ON);
            led_render_frame(50);
        }
    }
}

/**
 * @brief パターン描画・合成・90度回転テスト
 */
void test_pattern_and_rotation(void) {
    static const uint8_t heart_pattern[8] = {
        0b00000000,
        0b01100110,
        0b11111111,
        0b11111111,
        0b01111110,
        0b00111100,
        0b00011000,
        0b00000000
    };

    // 1. ハート型を描画
    led_buffer_draw_pattern(heart_pattern);
    led_render_frame(1500);

    // 2. 90度回転
    led_buffer_rotate_90();
    led_render_frame(1500);
}

/**
 * @brief 点滅制御のテスト
 */
void test_blink_control(void) {
    led_buffer_fill_all();
    led_render_frame(1000);
    led_render_blink(0, 0); // デフォルト点滅
}

/**
 * @brief 全単体テストの一括実行関数
 */
void run_all_unit_tests(void) {
    test_pixel_control();
    test_accumulate_dots_animation();
    test_one_by_one_dot_animation();
    test_pattern_and_rotation();
    test_blink_control();
}

// =============================================================
// メイン関数
// =============================================================
// ◆参考
// マトリクスLED構造※左上が原点（型番印刷側面を下とした時）
//     左上 x 0     1    2    3    4    5    6    7
// bytes[y]0 [0].x0,[0].x1
// bytes[y]1 [1].x0,[1].x1...
// bytes[y]2
// bytes[y]3
// bytes[y]4
// bytes[y]5
// bytes[y]6
// bytes[y]7

// ◆595テストコード（全行点灯させるダイナミック点灯せず、行固定）
#if TEST_MODE_595
int main(void) {
    // 1. ハードウェア初期化
    init_hardware();

    // 2. 74HC595テスト実行
	uint8_t row = 0; //0行目（一番上の行）を選択※138A,B,Cすべてに0をセット。
	uint8_t test_line_data = 0b10101010;
	hw_matrix_output_line(uint8_t row, uint8_t test_line_data); 

    return 0;
}
#endif

// ◆138テストコード（ゆっくりダイナミック全点灯）
// 使用場合は#define TEST_MODE_138 0⇒1へ書き換え。※他テストモード1⇒0も忘れずに
#if TEST_MODE_138
int main(void) {
    // 1. ハードウェア初期化
    init_hardware();

    // 2. 138テスト実行
	led_buffer_fill_all(); //ledバッファ全ピクセルON
	#undef ROW_SCAN_TIME_US 
	#define ROW_SCAN_TIME_US 1000*1000*5 //1000us=1ms,1000ms=1s※1行5秒
	led_render_scan_once(); //1行5秒点灯でダイナック点灯を実施。

	return 0;
}
#endif

// ◆一通り点灯パターン実行テストコード
#if TEST_MODE_ALL
int main(void) {
    // 1. ハードウェア初期化
    init_hardware();

    // 2. 単体テスト実行
    run_all_unit_tests();

    // 3. メインループ：電光掲示板モード（常時点灯スクロール）
    mode_ticker_board(scroll_text);

    return 0;
}
#endif

/* 一時コメントアウト
// -------------------------------------------------------------
// メイン関数
// -------------------------------------------------------------
int main()
{
    stdio_init_all();

    // SPI初期化 (100kHz)
    spi_init(SPI_PORT, 100 * 1000);

    // GPIOピン機能設定
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_RCK,  GPIO_FUNC_SIO);

    gpio_set_function(DMUX_A, GPIO_FUNC_SIO);
    gpio_set_function(DMUX_B, GPIO_FUNC_SIO);
    gpio_set_function(DMUX_C, GPIO_FUNC_SIO);

    // ピンの方向を出力に設定
    gpio_set_dir(PIN_RCK, GPIO_OUT);
    gpio_set_dir(DMUX_A,   GPIO_OUT);
    gpio_set_dir(DMUX_B,   GPIO_OUT);
    gpio_set_dir(DMUX_C,   GPIO_OUT);

    gpio_put(PIN_RCK, 0);

    // 現在画面に表示中の 8行分のフレームバッファ（初期状態は全消灯）
    uint8_t display_buffer[8] = {0};
    // ●uint8_t display_buffer[8] = {0};
    // 8×8マトリクスLEDに「今画面に映っているドット絵」情報を保持する
    //  8バイトのメモリ領域。（ここに入れた8列分のビットデータが点灯される）
    // 初期状態: {0} と書くことで、8行すべて 0x00（全消灯＝黒画面）からスタート。
    // ※74HC595はLow点灯だが、display_line関数でビット反転させるのでこれでOK。
    // ●[] = {0};
    // 配列を {} で初期化する際、指定した要素数が配列のサイズより少ない場合
    // 「書かれていない残りの要素は、すべて自動的に 0（または NULL）で初期化
    // しなければならない。

    // ■ASCII文字入力を使う場合は通常の strlen を使用する上行をONに
    size_t text_length = strlen(scroll_text);
    // size_t text_length = wcslen(scroll_text);
    // ●wcslen() 関数：
    // ワイド文字列の長さをワイド文字単位（文字数）で計算するC/C++の標準
    // ライブラリ関数。ここで20行目で指定した表示させたい、ワイド文字列の
    // 正確な文字数（要素数）を取得して size_t型の変数に保存。

    while (true)
    // ●一層目：電源が切れるまでメッセージを最初から繰り返す。
    {
        // 1文字ずつ順番に取り出す
        for (size_t char_idx = 0; char_idx < text_length; char_idx++)
        // 表示するメッセージから、0番目の文字（' '）、1番目の文字（'H'）、
        // 2番目の文字（'e'）…と、(文字列の長さ-1)まで1文字ずつ順番に
        // 処理対象を切り替える。
        {   
            // 【修正後】ASCII対応関数を呼び出す
            const uint8_t *next_font 
            = get_font_data_ascii(scroll_text[char_idx]);
            // ■UTF-8版を使う場合は↑コードコメントアウトして↓コードを逆にコメント外す。
            // 文字コードからフォントデータ（8バイト）を取得
            // const uint8_t *next_font 
            // = get_font_data(scroll_text[char_idx]);
            // ●get_font_data(...): 
            // 取り出した1文字（例: L'H'）を渡し、misaki_font.h の検索テーブルから、
            // その文字の8バイトLEDフォントデータへの先頭アドレス
            // （const uint8_t* ポインタ）を受け取る。
            // ※return font_table[i].data;で返ってくる。※dataは配列名=&data[0]

            // 1文字（8列）分、1ドットずつ左へシフト
            for (int col = 0; col < 8; col++) //column=列※縦方向
            {
                // 各行（0〜7行）のビットを左へ1ピクセル移動し、
                // 新しいドットを右端に追加
                for (int row = 0; row < 8; row++) //row=行※横方向
                {
                    uint8_t new_bit = (next_font[row] >> (7 - col)) & 0x01;
                    // ●① 1ピクセルを抽出する式
                    // ●next_font[row]: 
                    // 取得したフォントデータの row 行目（1バイト＝8ビット）。
                    // ●(7 - col): 
                    // フォントデータは 7ビット目が一番左のドット、
                    // 0ビット目が一番右のドット に対応している。
                    //  { L'\u0048', { 0xA0, 0xA0, 0xA0, 0xE0, 0xA0, 0xA0, 0xA0, 0x00 } },'H'
                    // next_font[n]=fonta_table.data[0]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[1]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[2]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[3]=0xA0=0b11100000
                    // next_font[n]=fonta_table.data[4]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[5]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[6]=0xA0=0b10100000
                    // next_font[n]=fonta_table.data[7]=0xA0=0b00000000
                    // ◆画面バッファの初期状態 display_buffer[3] =
                    //  0b00000000（全消灯）の時の動き。
                    // ●col = 0（最初の列）のとき ➔ 7 - 0 = 7
                    // （一番左のビットを抽出）
                    //●抽出式: new_bit = (0b1110 0000 >> (7-0))&0x01
                    // なので右へ7ビットシフト＝0b0000 0001➔new_bit = 1
                    //●結合式: display_buffer[3] =
                        //  (display_buffer[3] << 1) | new_bit
                    // もとの画面: 0b00000000
                    // << 1（左へ1ピクセルずらす） ➔ 0b00000000
                    //  | 1（論理和の結果、右端に 1が来る ） ➔ 0b00000001
                    // ➔ 画面の状態: ○ ○ ○ ○ ○ ○ ○ ● （右端にHの一番左の線が現れる）
                    // ●col = 7（最後の列）のとき ➔ 7 - 7 = 0（一番右のビットを抽出）
                    // >>（右シフト）と & 0x01（マスク演算）: 抽出したいビットを
                    // 右端（0ビット目）まで右シフトでずらし、0x01（00000001）と論理積（AND）を
                    // とることで、他の桁を消去して純粋な 1（点灯）か 0（消灯）だけを取り出す。
                    // この1ビットずらす作業を1～8行すべてに行う。

                    display_buffer[row] =
                     (display_buffer[row] << 1) | new_bit;
                    // ●display_buffer[row] << 1
                    // ➔画面全体をベルトコンベアのように1ピクセル左へ送り出している。
                    // ●| new_bit
                    // ➔押し出されて空いた右端の1ピクセル枠に、フォントからくり抜いた
                    // 次のドット（1か0）をパズルハメしている。
                    // ● (display_buffer[row] << 1): 
                    // 現在画面に映っている行データを 左に1ビット（1ピクセル）左シフト。
                    // これにより、画面全体のドットが左へ1ピクセル押し出され、
                    // 一番右端（0ビット目）が 0（空き）になる。
                    // | new_bit: 論理和（OR）演算子 を使い、空いた右端の0ビット目に、
                    // 先ほど抽出した新しい1ピクセル new_bit を追加する。
                }

                // 1ドットシフトした状態でダイナミック点灯（表示スピード調整）
                for (int frame = 0; frame < 18; frame++)
                {
                    for (uint8_t row = 0; row < 8; row++)
                    {   
                        // 【修正後】上下（7 - row）を渡す。
                        display_line(7 - row, display_buffer[row]);

                        // display_line(row, display_buffer[row]);
                        // ビット操作し作った表示行の点灯パターンを
                        //（表示行を選択＆その列の点灯パターンを）渡して点灯。
                        sleep_us(2000); // 2ms待機
                    }
                    // なにをしているのか？: 
                    // 1ピクセル左へ動かした状態の画面バッファ（display_buffer）
                    // を、人間の目で見える時間だけ静止して点灯保持
                    // （表示スピードの制御）。
                    // スピード決定の時間の計算display_line(...) ＋
                    //  sleep_us(2000): 1行分を描画して 2000μs（2ミリ秒）待機。
                    // この frame < 6 の 6 や sleep_us(2000) の数値を変更する
                    // ことで、スクロールのスピードを速くしたり遅くしたり出来る。
                }
            }
        }
    }
}
// 文字方向についてチェック。
//{ L'\u0041', { 0x40, 0xA0, 0xA0, 0xA0, 0xE0, 0xA0, 0xA0, 0x00 } }, // 'A'
// 0b0100 0000
// 0b1010 0000
// 0b1010 0000
// 0b1010 0000
// 0b1110 0000
// 0b1010 0000
// 0b1010 0000
// 0b0000 0000
// { L'\u3093', { 0x10, 0x10, 0x20, 0x30, 0x4A, 0x4A, 0x84, 0x00 } }, // 'ん'
// 0b0001 0000
// 0b0001 0000
// 0b0010 0000
// 0b0011 0000
// 0b0100 1010
// 0b0100 1010
// 0b0000 0000


一時コメントアウト*/
