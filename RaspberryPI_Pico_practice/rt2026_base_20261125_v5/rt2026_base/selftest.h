/**
 * @file  selftest.h
 * @brief 基板の動作確認メニューです。USBシリアルの画面から、部品を1つずつ確かめます。
 */
#ifndef SELFTEST_H
#define SELFTEST_H

/** メニューを表示し、'q' が押されるまで操作を受け付けます。main から呼びます。 */
void selftest_menu(void);

#endif /* SELFTEST_H */
