#ifndef SSD1306_CUSTOM_H
#define SSD1306_CUSTOM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Core includes - replace with the HAL header your project uses if different */
#include <stdint.h>
#include <stddef.h>
#include "stm32f1xx_hal.h" /* change to your MCU family HAL header if required */

/* I2C handle used by implementation (externally defined in your project) */
extern I2C_HandleTypeDef hi2c1;

/* SSD1306 7-bit address often 0x3C; HAL expects 8-bit shifted address */
#ifndef SSD1306_ADDR
#define SSD1306_ADDR (0x3C << 1)
#endif

/* Configurable transport parameters (override before including this header) */
#ifndef SSD1306_I2C_CHUNK_SIZE
#define SSD1306_I2C_CHUNK_SIZE 32
#endif

#ifndef SSD1306_I2C_TIMEOUT_MS
#define SSD1306_I2C_TIMEOUT_MS 200
#endif

#ifndef SSD1306_I2C_RETRIES
#define SSD1306_I2C_RETRIES 2
#endif

/* --- 5x8 font (provided in source) ------------------------------------- */
#define FONT_FIRST_CHAR 32
#define FONT_COUNT 96
extern const uint8_t font5x8[FONT_COUNT][5];

/* --- 7x10 custom font (provided in source) ------------------------------ */
#define FONT7X10_FIRST_CHAR 32
#define FONT7X10_COUNT 96
#define FONT7X10_ROWS 10
#define FONT7X10_COLS 7
extern const uint16_t Font7x10[]; /* layout: FONT7X10_ROWS uint16_t per glyph */

/* --- API ----------------------------------------------------------------
   Notes:
   - HAL_StatusTypeDef is the return type from HAL I2C functions (defined in HAL).
   - Coordinates:
       col  : pixel column 0..127
       page : page 0..7 (8-pixel-high rows). 7x10 glyphs occupy two pages (page and page+1)
-------------------------------------------------------------------------*/

/* Low level command / data primitives */
HAL_StatusTypeDef ssd1306_command(uint8_t cmd);
HAL_StatusTypeDef ssd1306_data(uint8_t *data, uint16_t size);

/* Cursor control */
void ssd1306_set_cursor(uint8_t page, uint8_t col);

/* Basic display control */
void ssd1306_init(void);
HAL_StatusTypeDef ssd1306_clear(void);

/* 5x8 font helpers (each glyph 5 bytes, stored in font5x8) */
HAL_StatusTypeDef ssd1306_write_char(uint8_t col, uint8_t page, char c);
HAL_StatusTypeDef ssd1306_write_string(uint8_t col, uint8_t page, const char *s);

/* 7x10 custom font helpers (Font7x10, converts glyphs to two pages) */
HAL_StatusTypeDef ssd1306_write_char_from_Font7x10cust(uint8_t col, uint8_t page, char c);
HAL_StatusTypeDef ssd1306_write_string_7x10cust(uint8_t start_col, uint8_t page, const char *s);

/* Utility (optional) - exposes internal helper used by 7x10 conversion */
int ssd1306_font7x10_find_bit_offset(void);

void Display_ShowCoordinates(float lat, float lon, float alt);


/* If you want to compile the implementation as C++ code, define this to 1 to
   avoid name collisions with other SSD1306 drivers. */
#ifndef SSD1306_CUSTOM_NAMESPACE
#define SSD1306_CUSTOM_NAMESPACE 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_CUSTOM_H */
