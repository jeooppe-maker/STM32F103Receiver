#include "ssd1306.h"
#include <string.h>

/* External HAL I2C handle from the main project */
extern I2C_HandleTypeDef hi2c1;

/* Fonts are provided in other translation units (user code) */
extern const uint8_t font5x8[FONT_COUNT][5];
extern const uint16_t Font7x10[]; /* FONT7X10_ROWS uint16_t per glyph */

/* Internal constants for 7x10 conversion */
#ifndef FONT7X10_ROWS
#define FONT7X10_ROWS 10
#endif
#ifndef FONT7X10_COLS
#define FONT7X10_COLS 7
#endif

/* Forward declarations of internal helpers */
static int ssd1306_font7x10_find_bit_offset_internal(void);
static void ssd1306_font7x10_glyph_to_pages_internal(const uint16_t glyph_rows[FONT7X10_ROWS],
                                                     uint8_t out_page0[FONT7X10_COLS + 1],
                                                     uint8_t out_page1[FONT7X10_COLS + 1],
                                                     int bit_offset);

/* ----------------------------------------------------------------------------
   Low level I2C primitives
   ---------------------------------------------------------------------------- */

/* Send one command byte (control byte = 0x00) */
HAL_StatusTypeDef ssd1306_command(uint8_t cmd)
{
    uint8_t buf[2];
    buf[0] = 0x00; /* Co = 0, D/C# = 0 -> command */
    buf[1] = cmd;
    return HAL_I2C_Master_Transmit(&hi2c1, SSD1306_ADDR, buf, 2, SSD1306_I2C_TIMEOUT_MS);
}

/* Send data buffer with control byte 0x40. This implementation chunks the
   payload to SSD1306_I2C_CHUNK_SIZE bytes per transfer. */
HAL_StatusTypeDef ssd1306_data(uint8_t *data, uint16_t size)
{
    if (data == NULL || size == 0) return HAL_OK;

    uint8_t txbuf[SSD1306_I2C_CHUNK_SIZE + 1];
    uint16_t sent = 0;
    HAL_StatusTypeDef status = HAL_OK;

    while (sent < size) {
        uint16_t chunk = (uint16_t)((size - sent) > SSD1306_I2C_CHUNK_SIZE ? SSD1306_I2C_CHUNK_SIZE : (size - sent));
        txbuf[0] = 0x40; /* Co = 0, D/C# = 1 -> data */
        memcpy(&txbuf[1], &data[sent], chunk);

        int attempt;
        for (attempt = 0; attempt <= SSD1306_I2C_RETRIES; attempt++) {
            status = HAL_I2C_Master_Transmit(&hi2c1, SSD1306_ADDR, txbuf, (uint16_t)(chunk + 1), SSD1306_I2C_TIMEOUT_MS);
            if (status == HAL_OK) break;
            HAL_Delay(5);
        }

        if (status != HAL_OK) return status;

        sent += chunk;
    }

    return HAL_OK;
}

/* ----------------------------------------------------------------------------
   Cursor control
   ---------------------------------------------------------------------------- */

/* Set page (0..7) and column (0..127) for subsequent data writes */
void ssd1306_set_cursor(uint8_t page, uint8_t col)
{
    if (page > 7) page = 7;
    if (col  > 127) col = 127;

    /* Page address B0h..B7h */
    ssd1306_command(0xB0 | (page & 0x07));

    /* Lower column address (0..15) */
    ssd1306_command(0x00 | (col & 0x0F));

    /* Higher column address (0..7) */
    ssd1306_command(0x10 | ((col >> 4) & 0x0F));
}

/* ----------------------------------------------------------------------------
   High level display helpers
   ---------------------------------------------------------------------------- */

void ssd1306_init(void)
{
    HAL_Delay(50);

    ssd1306_command(0xAE);             /* Display OFF */
    ssd1306_command(0x20); ssd1306_command(0x00); /* Memory addressing mode: Horizontal */
    ssd1306_command(0xB0);             /* Page start address (B0h) */
    ssd1306_command(0xC8);             /* COM Output Scan Direction: remapped */
    ssd1306_command(0x00);             /* Low column address */
    ssd1306_command(0x10);             /* High column address */
    ssd1306_command(0x40);             /* Start line address */
    ssd1306_command(0x81); ssd1306_command(0x7F); /* Contrast control */
    ssd1306_command(0xA1);             /* Segment remap */
    ssd1306_command(0xA6);             /* Normal display */
    ssd1306_command(0xA8); ssd1306_command(0x3F); /* Multiplex ratio 1/64 */
    ssd1306_command(0xA4);             /* Display follow RAM content */
    ssd1306_command(0xD3); ssd1306_command(0x00); /* Display offset */
    ssd1306_command(0xD5); ssd1306_command(0x80); /* Display clock divide/oscillator */
    ssd1306_command(0xD9); ssd1306_command(0xF1); /* Pre-charge period */
    ssd1306_command(0xDA); ssd1306_command(0x12); /* COM pins hw config */
    ssd1306_command(0xDB); ssd1306_command(0x40); /* VCOMH deselect level */
    ssd1306_command(0x8D); ssd1306_command(0x14); /* Charge pump setting (enable) */
    ssd1306_command(0xAF);             /* Display ON */

    HAL_Delay(10);
}

HAL_StatusTypeDef ssd1306_clear(void)
{
    uint8_t line[128];
    memset(line, 0x00, sizeof(line));

    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_set_cursor(page, 0);
        HAL_StatusTypeDef st = ssd1306_data(line, sizeof(line));
        if (st != HAL_OK) return st;
    }
    return HAL_OK;
}

/* ----------------------------------------------------------------------------
   5x8 font rendering (font5x8)
   ---------------------------------------------------------------------------- */

/* Write single 5x8 character (5 bytes + 1 spacing) at pixel col, page */
HAL_StatusTypeDef ssd1306_write_char(uint8_t col, uint8_t page, char c)
{
    uint8_t uc = (uint8_t)c;
    int index = (int)uc - FONT_FIRST_CHAR;
    if (index < 0 || index >= FONT_COUNT) index = 0;

    uint8_t glyph[6];
    memcpy(glyph, font5x8[index], 5);
    glyph[5] = 0x00;

    ssd1306_set_cursor(page, col);
    return ssd1306_data(glyph, sizeof(glyph));
}

/* Write null-terminated string using 5x8 font. col in pixels, page 0..7 */
HAL_StatusTypeDef ssd1306_write_string(uint8_t col, uint8_t page, const char *s)
{
    if (s == NULL) return HAL_OK;
    uint8_t x = col;

    while (*s) {
        HAL_StatusTypeDef res = ssd1306_write_char(x, page, *s++);
        if (res != HAL_OK) return res;
        x += 6; /* 5 pixels glyph + 1 pixel spacing */
        if (x >= 128) break;
    }
    return HAL_OK;
}

/* ----------------------------------------------------------------------------
   7x10 custom font rendering (Font7x10)
   ---------------------------------------------------------------------------- */

/* Public wrapper for the internal bit-offset finder (returns 0..15) */
int ssd1306_font7x10_find_bit_offset(void)
{
    return ssd1306_font7x10_find_bit_offset_internal();
}

/* Write a single character from Font7x10. Character occupies two pages:
   page and page+1. col in pixels (0..127). */
HAL_StatusTypeDef ssd1306_write_char_from_Font7x10cust(uint8_t col, uint8_t page, char c)
{
    static int bit_offset = -1;
    if (bit_offset < 0) bit_offset = ssd1306_font7x10_find_bit_offset_internal();

    int uc = (uint8_t)c;
    int idx = uc - FONT7X10_FIRST_CHAR;
    if (idx < 0 || idx >= FONT7X10_COUNT) idx = 0;

    const uint16_t *glyph_rows = &Font7x10[idx * FONT7X10_ROWS];

    uint8_t page0[FONT7X10_COLS + 1];
    uint8_t page1[FONT7X10_COLS + 1];
    memset(page0, 0x00, sizeof(page0));
    memset(page1, 0x00, sizeof(page1));

    ssd1306_font7x10_glyph_to_pages_internal(glyph_rows, page0, page1, bit_offset);

    /* send top page (rows 0..7) */
    ssd1306_set_cursor(page, col);
    HAL_StatusTypeDef r = ssd1306_data(page0, FONT7X10_COLS + 1);
    if (r != HAL_OK) return r;

    /* send bottom page (rows 8..9 -> bits 0..1 within a byte) */
    ssd1306_set_cursor(page + 1, col);
    return ssd1306_data(page1, FONT7X10_COLS + 1);
}

/* Write string using 7x10 font. Each glyph width = FONT7X10_COLS + 1 spacing */
HAL_StatusTypeDef ssd1306_write_string_7x10cust(uint8_t start_col, uint8_t page, const char *s)
{
    if (s == NULL) return HAL_OK;
    HAL_StatusTypeDef res = HAL_OK;
    uint8_t x = start_col;
    while (*s && (x + FONT7X10_COLS + 1) <= 128) {
        res = ssd1306_write_char_from_Font7x10cust(x, page, *s++);
        if (res != HAL_OK) return res;
        x += (FONT7X10_COLS + 1);
    }
    return res;
}

/* ----------------------------------------------------------------------------
   Internal helpers for 7x10 font conversion
   ---------------------------------------------------------------------------- */

/* Find smallest set bit across all glyph rows to compute bit offset */
static int ssd1306_font7x10_find_bit_offset_internal(void)
{
    for (int s = 0; s < FONT7X10_COUNT; s++) {
        const uint16_t *g = &Font7x10[s * FONT7X10_ROWS];
        for (int r = 0; r < FONT7X10_ROWS; r++) {
            uint16_t v = g[r];
            if (v == 0) continue;
            for (int b = 0; b < 16; b++) {
                if (v & (1u << b)) return b;
            }
        }
    }
    return 0;
}

/* Convert 10 rows of 16-bit-wide glyph data into two page buffers:
   - out_page0: FONT7X10_COLS bytes mapping rows 0..7 into bits 0..7
   - out_page1: FONT7X10_COLS bytes mapping rows 8..9 into bits 0..1
   The final byte in each out_page is the empty spacing column (0x00).
   This implementation mirrors the column order (6 - col) to match the
   Font7x10 storage format used by the user.
*/
static void ssd1306_font7x10_glyph_to_pages_internal(const uint16_t glyph_rows[FONT7X10_ROWS],
                                                     uint8_t out_page0[FONT7X10_COLS + 1],
                                                     uint8_t out_page1[FONT7X10_COLS + 1],
                                                     int bit_offset)
{
    for (int col = 0; col < FONT7X10_COLS; col++) {
        uint8_t p0 = 0;
        uint8_t p1 = 0;

        /* top 8 rows -> bits 0..7 of p0 */
        for (int row = 0; row < 8; row++) {
            if (row < FONT7X10_ROWS) {
                uint16_t rowval = glyph_rows[row];
                uint16_t shifted = (uint16_t)(rowval >> bit_offset);
                /* flip column order */
                if (shifted & (1u << (6 - col))) p0 |= (1u << row);
            }
        }

        /* rows 8..9 -> bits 0..1 of p1 */
        for (int row = 8; row < FONT7X10_ROWS; row++) {
            uint16_t rowval = glyph_rows[row];
            uint16_t shifted = (uint16_t)(rowval >> bit_offset);
            if (shifted & (1u << (6 - col))) p1 |= (1u << (row - 8));
        }

        out_page0[col] = p0;
        out_page1[col] = p1;
    }

    /* spacing column */
    out_page0[FONT7X10_COLS] = 0x00;
    out_page1[FONT7X10_COLS] = 0x00;
}

const uint8_t font5x8[FONT_COUNT][5] = { /* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00}, /* 0x21 '!' */ {0x00,0x00,0x5F,0x00,0x00}, /* 0x22 '"' */ {0x00,0x07,0x00,0x07,0x00}, /* 0x23 '#' */ {0x14,0x7F,0x14,0x7F,0x14}, /* 0x24 '$' */ {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x25 '%' */ {0x23,0x13,0x08,0x64,0x62}, /* 0x26 '&' */ {0x36,0x49,0x55,0x22,0x50}, /* 0x27 '\''*/ {0x00,0x05,0x03,0x00,0x00}, /* 0x28 '(' */ {0x00,0x1C,0x22,0x41,0x00}, /* 0x29 ')' */ {0x00,0x41,0x22,0x1C,0x00}, /* 0x2A '*' */ {0x14,0x08,0x3E,0x08,0x14}, /* 0x2B '+' */ {0x08,0x08,0x3E,0x08,0x08}, /* 0x2C ',' */ {0x00,0x50,0x30,0x00,0x00}, /* 0x2D '-' */ {0x08,0x08,0x08,0x08,0x08}, /* 0x2E '.' */ {0x00,0x60,0x60,0x00,0x00}, /* 0x2F '/' */ {0x20,0x10,0x08,0x04,0x02}, /* 0x30 '0' */ {0x3E,0x51,0x49,0x45,0x3E}, /* 0x31 '1' */ {0x00,0x42,0x7F,0x40,0x00}, /* 0x32 '2' */ {0x42,0x61,0x51,0x49,0x46}, /* 0x33 '3' */ {0x21,0x41,0x45,0x4B,0x31}, /* 0x34 '4' */ {0x18,0x14,0x12,0x7F,0x10}, /* 0x35 '5' */ {0x27,0x45,0x45,0x45,0x39}, /* 0x36 '6' */ {0x3C,0x4A,0x49,0x49,0x30}, /* 0x37 '7' */ {0x01,0x71,0x09,0x05,0x03}, /* 0x38 '8' */ {0x36,0x49,0x49,0x49,0x36}, /* 0x39 '9' */ {0x06,0x49,0x49,0x29,0x1E}, /* 0x3A ':' */ {0x00,0x36,0x36,0x00,0x00}, /* 0x3B ';' */ {0x00,0x56,0x36,0x00,0x00}, /* 0x3C '<' */ {0x08,0x14,0x22,0x41,0x00}, /* 0x3D '=' */ {0x14,0x14,0x14,0x14,0x14}, /* 0x3E '>' */ {0x00,0x41,0x22,0x14,0x08}, /* 0x3F '?' */ {0x02,0x01,0x51,0x09,0x06}, /* 0x40 '@' */ {0x32,0x49,0x79,0x41,0x3E}, /* 0x41 'A' */ {0x7E,0x11,0x11,0x11,0x7E}, /* 0x42 'B' */ {0x7F,0x49,0x49,0x49,0x36}, /* 0x43 'C' */ {0x3E,0x41,0x41,0x41,0x22}, /* 0x44 'D' */ {0x7F,0x41,0x41,0x22,0x1C}, /* 0x45 'E' */ {0x7F,0x49,0x49,0x49,0x41}, /* 0x46 'F' */ {0x7F,0x09,0x09,0x09,0x01}, /* 0x47 'G' */ {0x3E,0x41,0x49,0x49,0x7A}, /* 0x48 'H' */ {0x7F,0x08,0x08,0x08,0x7F}, /* 0x49 'I' */ {0x00,0x41,0x7F,0x41,0x00}, /* 0x4A 'J' */ {0x20,0x40,0x41,0x3F,0x01}, /* 0x4B 'K' */ {0x7F,0x08,0x14,0x22,0x41}, /* 0x4C 'L' */ {0x7F,0x40,0x40,0x40,0x40}, /* 0x4D 'M' */ {0x7F,0x02,0x0C,0x02,0x7F}, /* 0x4E 'N' */ {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4F 'O' */ {0x3E,0x41,0x41,0x41,0x3E}, /* 0x50 'P' */ {0x7F,0x09,0x09,0x09,0x06}, /* 0x51 'Q' */ {0x3E,0x41,0x51,0x21,0x5E}, /* 0x52 'R' */ {0x7F,0x09,0x19,0x29,0x46}, /* 0x53 'S' */ {0x46,0x49,0x49,0x49,0x31}, /* 0x54 'T' */ {0x01,0x01,0x7F,0x01,0x01}, /* 0x55 'U' */ {0x3F,0x40,0x40,0x40,0x3F}, /* 0x56 'V' */ {0x1F,0x20,0x40,0x20,0x1F}, /* 0x57 'W' */ {0x7F,0x20,0x18,0x20,0x7F}, /* 0x58 'X' */ {0x63,0x14,0x08,0x14,0x63}, /* 0x59 'Y' */ {0x03,0x04,0x78,0x04,0x03}, /* 0x5A 'Z' */ {0x61,0x51,0x49,0x45,0x43}, /* 0x5B '[' */ {0x00,0x7F,0x41,0x41,0x00}, /* 0x5C '\' */ {0x02,0x04,0x08,0x10,0x20}, /* 0x5D ']' */ {0x00,0x41,0x41,0x7F,0x00}, /* 0x5E '^' */ {0x04,0x02,0x01,0x02,0x04}, /* 0x5F '_' */ {0x40,0x40,0x40,0x40,0x40}, /* 0x60 '`' */ {0x00,0x01,0x02,0x04,0x00}, /* 0x61 'a' */ {0x20,0x54,0x54,0x54,0x78}, /* 0x62 'b' */ {0x7F,0x48,0x44,0x44,0x38}, /* 0x63 'c' */ {0x38,0x44,0x44,0x44,0x20}, /* 0x64 'd' */ {0x38,0x44,0x44,0x48,0x7F}, /* 0x65 'e' */ {0x38,0x54,0x54,0x54,0x18}, /* 0x66 'f' */ {0x08,0x7E,0x09,0x01,0x02}, /* 0x67 'g' */ {0x0C,0x52,0x52,0x52,0x3E}, /* 0x68 'h' */ {0x7F,0x08,0x04,0x04,0x78}, /* 0x69 'i' */ {0x00,0x44,0x7D,0x40,0x00}, /* 0x6A 'j' */ {0x20,0x40,0x44,0x3D,0x00}, /* 0x6B 'k' */ {0x7F,0x10,0x28,0x44,0x00}, /* 0x6C 'l' */ {0x00,0x41,0x7F,0x40,0x00}, /* 0x6D 'm' */ {0x7C,0x04,0x18,0x04,0x78}, /* 0x6E 'n' */ {0x7C,0x08,0x04,0x04,0x78}, /* 0x6F 'o' */ {0x38,0x44,0x44,0x44,0x38}, /* 0x70 'p' */ {0x7C,0x14,0x14,0x14,0x08}, /* 0x71 'q' */ {0x08,0x14,0x14,0x18,0x7C}, /* 0x72 'r' */ {0x7C,0x08,0x04,0x04,0x08}, /* 0x73 's' */ {0x48,0x54,0x54,0x54,0x20}, /* 0x74 't' */ {0x04,0x3F,0x44,0x40,0x20}, /* 0x75 'u' */ {0x3C,0x40,0x40,0x20,0x7C}, /* 0x76 'v' */ {0x1C,0x20,0x40,0x20,0x1C}, /* 0x77 'w' */ {0x3C,0x40,0x30,0x40,0x3C}, /* 0x78 'x' */ {0x44,0x28,0x10,0x28,0x44}, /* 0x79 'y' */ {0x0C,0x50,0x50,0x50,0x3C}, /* 0x7A 'z' */ {0x44,0x64,0x54,0x4C,0x44}, /* 0x7B '{' */ {0x00,0x08,0x36,0x41,0x00}, /* 0x7C '|' */ {0x00,0x00,0x7F,0x00,0x00}, /* 0x7D '}' */ {0x00,0x41,0x36,0x08,0x00}, /* 0x7E '~' */ {0x02,0x01,0x02,0x04,0x02}, /* 0x7F DEL */ {0x00,0x06,0x09,0x09,0x06} };


#include <stdio.h>
#include <string.h>
/* Допоміжна: форматування float у рядок з 6 знаками після коми без %f */
static void ftoa6(float v, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    /* знак */
    if (v < 0) {
        v = -v;
        if (out_len > 0) {
            *out++ = '-';
            out_len--;
        }
    }
    uint32_t whole = (uint32_t)v;
    float frac = v - (float)whole;
    uint32_t frac6 = (uint32_t)(frac * 1000000.0f + 0.5f);
    /* у випадку округлення, коли frac6 == 1000000 */
    if (frac6 >= 1000000) {
        whole += 1;
        frac6 = 0;
    }
    /* формуємо рядок вручну */
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu.%06lu", (unsigned long)whole, (unsigned long)frac6);
    /* копіюємо з урахуванням довжини */
    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
}

/* Перетворення у формат dd mm.mmm' (альтернативний варіант) */
static void latlon_to_dms_str(float deg, char hemi, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    char sign = (deg < 0) ? '-' : '+';
    if (deg < 0) deg = -deg;
    int d = (int)deg;
    float minutes = (deg - d) * 60.0f;
    int m_whole = (int)minutes;
    int m_frac = (int)((minutes - m_whole) * 1000.0f + 0.5f); /* 3 знаки для хвилин */
    snprintf(out, out_len, "%02d°%02d.%03d' %c", d, m_whole, m_frac, hemi);
}

/* Реалізація показу координат */
void Display_ShowCoordinates(float lat, float lon, float alt)
{
    char line0[32];
    char line1[32];
    char line2[32];

    /* Перший рядок — заголовок / дата за бажанням */
    strncpy(line0, "GPS:", sizeof(line0)-1);
    line0[sizeof(line0)-1] = '\0';

    /* Другий рядок — широта */
    char lat_s[24];
    char lon_s[24];
    char alt_s[16];

    /* Визначаємо півкулю */
    char lat_hemi = (lat < 0.0f) ? 'S' : 'N';
    char lon_hemi = (lon < 0.0f) ? 'W' : 'E';

    /* Форматування — варіант з 6 десятковими */
    ftoa6((lat < 0) ? -lat : lat, lat_s, sizeof(lat_s));
    ftoa6((lon < 0) ? -lon : lon, lon_s, sizeof(lon_s));
    /* alt */
    {
        int alt_i = (int)(alt + 0.5f);
        snprintf(alt_s, sizeof(alt_s), "%d m", alt_i);
    }

    /* Склеюємо рядки для дисплея (залежить від ваших ширини/рядків) */
    snprintf(line1, sizeof(line1), "Lat:%s %c", lat_s, lat_hemi);
    snprintf(line2, sizeof(line2), "Lon:%s %c", lon_s, lon_hemi);

    /* Очищаємо дисплей та виводимо (API вашої бібліотеки) */
    ssd1306_clear();
    ssd1306_write_string(0, 0, line0); /* рядок 0 */
    ssd1306_write_string(0, 1, line1); /* рядок 1 */
    ssd1306_write_string(0, 2, line2); /* рядок 2 */
    ssd1306_write_string(0, 3, alt_s); /* рядок 3 */

    /* Оновити екран якщо ваша бібліотека потребує виклику update */
    // SSD1306_UpdateScreen(); /* або ssd1306_update(), залежить від реалізації */
}
