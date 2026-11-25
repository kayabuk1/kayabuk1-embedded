/**
 * @file  gyro.c
 * @brief ジャイロ L3GD20 を読みます。【仮のドライバ】 説明は gyro.h にあります。
 */
#include "gyro.h"

#include "board.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define REG_WHO_AM_I  0x0Fu
#define REG_CTRL_REG1 0x20u
#define REG_CTRL_REG4 0x23u
#define REG_OUT_Z_L   0x2Cu
#define READ_BIT      0x80u /* アドレスの最上位ビットを1にすると、読み出し                 */
#define AUTO_INC_BIT  0x40u /* その次のビットを1にすると、続けて読むたびにアドレスが1進む */
#define DPS_PER_DIGIT 0.070f /* ±2000deg/s のとき、1目盛りは 70mdps（仕様書 3.3 の表） */

/** 1回の通信をします。SPI1 は A/D変換IC（モード0）と共用なので、毎回モード3に設定し直します。 */
static void transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_set_format(BOARD_SPI1_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_put(PIN_GYRO_CS, 0);
    spi_write_read_blocking(BOARD_SPI1_PORT, tx, rx, len);
    gpio_put(PIN_GYRO_CS, 1);
}

static void write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t tx[2] = {reg, value};
    uint8_t rx[2];
    transfer(tx, rx, 2);
}

static uint8_t who_am_i_value = 0; /* gyro_init() のときに読んだ値 */

uint8_t gyro_who_am_i(void)
{
    return who_am_i_value;
}

bool gyro_init(void)
{
    gpio_init(PIN_GYRO_CS);
    gpio_set_dir(PIN_GYRO_CS, GPIO_OUT);
    gpio_put(PIN_GYRO_CS, 1);

    write_reg(REG_CTRL_REG1, 0xFFu); /* 760Hz、帯域100Hz、電源ON、X・Y・Z軸を有効にする */
    write_reg(REG_CTRL_REG4, 0xA0u); /* 上位と下位を同じ回の値にそろえる（BDU）、±2000deg/s */

    const uint8_t tx[2] = {(uint8_t)(REG_WHO_AM_I | READ_BIT), 0};
    uint8_t rx[2] = {0, 0};
    transfer(tx, rx, 2);
    who_am_i_value = rx[1];
    return who_am_i_value == 0xD4u || who_am_i_value == 0xD7u;
}

float gyro_read_z_dps(void)
{
    const uint8_t tx[3] = {(uint8_t)(REG_OUT_Z_L | READ_BIT | AUTO_INC_BIT), 0, 0};
    uint8_t rx[3] = {0, 0, 0};
    transfer(tx, rx, 3);
    /* 下位のバイト、上位のバイトの順に届きます。 */
    const int16_t raw = (int16_t)(((uint16_t)rx[2] << 8) | rx[1]);
    return (float)raw * DPS_PER_DIGIT;
}
