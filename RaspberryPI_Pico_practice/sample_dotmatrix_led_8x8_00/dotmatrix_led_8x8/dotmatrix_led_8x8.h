#include "hardware/spi.h"

//dotmatrix led 
#define DOTMATRIX_ROWS  8
#define DOTMATRIX_COLS  8

// 74HC138入力ピン割り当て
#define HC138_PIN_A   19
#define HC138_PIN_B   20
#define HC138_PIN_C   21

#define SPI_PORT spi0
#define PIN_MOSI 7
#define PIN_SCK 6
#define PIN_LATCH 3

//prototype
void init_spi(void);
void hc595_write(uint8_t data);