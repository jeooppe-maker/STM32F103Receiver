#include "e32.h"
#include <stdio.h>
#include <string.h>

uint8_t LoRa_RX_Buffer[64];
char rx_line[RX_LINE_MAX];
uint8_t rx_idx = 0;

void E32_WaitAUX(void)
{
    while(HAL_GPIO_ReadPin(E32_AUX_PORT, E32_AUX_PIN) == GPIO_PIN_RESET);
}

// --- Встановлюємо режим модуля ---
void E32_SetMode(E32_Mode mode)
{
    switch(mode)
    {
        case E32_MODE_NORMAL:
            HAL_GPIO_WritePin(E32_M0_PORT, E32_M0_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(E32_M1_PORT, E32_M1_PIN, GPIO_PIN_RESET);
            break;
        case E32_MODE_WAKEUP:
            HAL_GPIO_WritePin(E32_M0_PORT, E32_M0_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(E32_M1_PORT, E32_M1_PIN, GPIO_PIN_RESET);
            break;
        case E32_MODE_POWERDOWN:
            HAL_GPIO_WritePin(E32_M0_PORT, E32_M0_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(E32_M1_PORT, E32_M1_PIN, GPIO_PIN_SET);
            break;
        case E32_MODE_PROGRAM:
            HAL_GPIO_WritePin(E32_M0_PORT, E32_M0_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(E32_M1_PORT, E32_M1_PIN, GPIO_PIN_SET);
            break;
    }
    HAL_Delay(50); // даємо час на переключення режиму
}

// --- Перевірка готовності через AUX ---
uint8_t E32_IsReady(void)
{
    return (HAL_GPIO_ReadPin(E32_AUX_PORT, E32_AUX_PIN) == GPIO_PIN_SET);
}

// --- Відправка рядка ---
void E32_SendString(char *str)
{
    while(!E32_IsReady()) HAL_Delay(5);
    HAL_UART_Transmit(&huart1, (uint8_t*)str, strlen(str), HAL_MAX_DELAY);
}

// --- Відправка одного байта ---
void E32_SendByte(uint8_t data)
{
    while(!E32_IsReady()) HAL_Delay(5);
    HAL_UART_Transmit(&huart1, &data, 1, HAL_MAX_DELAY);
}
// Callback — викликається при кожному прийнятому байті
uint8_t Packet[64];
uint8_t idx = 0;


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        uint8_t b = LoRa_RX_Buffer[0];

        // Після завершення прийому рядка
        if (b == '\n' || rx_idx >= RX_LINE_MAX-1)
        {
            rx_line[rx_idx] = 0;  // завершити рядок

            char lat[16] = {0};
            char lon[16] = {0};
            char alt[16] = {0};
            char speed[16] = {0};

            // Пропускаємо перший символ, якщо він зайвий
            char *p = rx_line;
            if (rx_line[0] < '0' || rx_line[0] > '9') p++;

            // Витягуємо числа з рядка, обрізаємо перед комою
            sscanf(p, "Lat:%15[^,],Lon:%15[^,],Alt:%15[^,],Speed:%15s",
                   lat, lon, alt, speed);

            // Вивід на OLED, по рядках
            ssd1306_clear();
            ssd1306_write_string(0, 0, lat);
            ssd1306_write_string(0, 2, lon);
            ssd1306_write_string(0, 4, alt);
            ssd1306_write_string(0, 6, speed);

            rx_idx = 0;
        }

        else
        {
            rx_line[rx_idx++] = b;
        }

        HAL_UART_Receive_IT(&huart1, LoRa_RX_Buffer, 1);
    }
}

//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//    if (huart->Instance == USART2)
//    {
//        uint8_t b = LoRa_RX_Buffer[0];
//
//        // Перевірка на кінець пакету (Packet Mode відправляє весь рядок як пакет)
//        if (b == '\n' || rx_idx >= RX_LINE_MAX-1)
//        {
//            rx_line[rx_idx] = 0; // завершити рядок
//
//            char lat[16] = {0};
//            char lon[16] = {0};
//            char alt[16] = {0};
//            char speed[16] = {0};
//
//            sscanf(rx_line, "Lat:%15[^,],Lon:%15[^,],Alt:%15[^,],Speed:%15s",
//                   lat, lon, alt, speed);
//
//            // Читаємо RSSI
//            uint8_t rssi = E32_ReadRSSI();
//
//            // OLED вивід
//            ssd1306_clear();
//            ssd1306_write_string(0, 0, lat);
//            ssd1306_write_string(0, 2, lon);
//            ssd1306_write_string(0, 4, alt);
//            ssd1306_write_string(0, 6, speed);
//
//            char rssi_str[8];
//            sprintf(rssi_str, "RSSI:%ddB", rssi);
//            ssd1306_write_string(0, 7, rssi_str);
//
//            rx_idx = 0;
//        }
//        else
//        {
//            rx_line[rx_idx++] = b;
//        }
//
//        HAL_UART_Receive_IT(&huart1, LoRa_RX_Buffer, 1);
//    }
//}
//

// -------------------------
// OLED custom functions
// -------------------------
void oled_print_char(char c)
{
    static uint8_t x = 0;
    static uint8_t y = 0;

    // якщо дійшли до кінця рядка
    if (x > 120) {
        x = 0;
        y += 8;
        if (y > 56) {    // дисплей 128x64 → 8 рядків
            y = 0;
            ssd1306_clear();
        }
    }

    char buf[2] = { c, 0 };
    ssd1306_write_string(x, y, buf); // Вивести символ
    x += 6; // ширина символа 5x8 + 1
}



uint8_t E32_ReadRSSI(void)
{
    uint8_t buffer[128];
    int len = HAL_UART_Receive(&huart1, buffer, sizeof(buffer), 50);
    if(len > 0)
    {
        return buffer[len-1]; // останній байт – RSSI
    }
    return 0;
}
void E32_ReadConfig(uint8_t *cfg)
{
    uint8_t cmd[3] = {0xC1, 0xC1, 0xC1};

    E32_SetMode(E32_MODE_PROGRAM);
    HAL_Delay(80);

    E32_WaitAUX();
    HAL_UART_Transmit(&huart1, cmd, 3, HAL_MAX_DELAY);

    E32_WaitAUX();
    HAL_UART_Receive(&huart1, cfg, 6, HAL_MAX_DELAY);

    E32_SetMode(E32_MODE_NORMAL);
}

void E32_ResetToFactory(void)
{
    uint8_t cmd[3] = {0xC4, 0xC4, 0xC4};

    E32_SetMode(E32_MODE_PROGRAM);
    HAL_Delay(80);
    E32_WaitAUX();

    HAL_UART_Transmit(&huart1, cmd, 3, HAL_MAX_DELAY);
    E32_WaitAUX(); // чекаємо завершення перезапису

    E32_SetMode(E32_MODE_NORMAL);
    HAL_Delay(50);
}

void E32_SetDefaultConfig(void)
{
    uint8_t default_cfg[6] = {0xC0, 0x00, 0x00, 0x1A, 0x17, 0x44};

    // --- Переводимо модуль у режим програмування ---
    E32_SetMode(E32_MODE_PROGRAM);
    E32_WaitAUX();   // чекаємо готовності
    HAL_Delay(200);

    // --- Надсилаємо дефолтну конфігурацію ---
    HAL_UART_Transmit(&huart1, default_cfg, 6, HAL_MAX_DELAY);
    E32_WaitAUX();   // чекаємо завершення запису
    HAL_Delay(200);
    // --- Повертаємо модуль у нормальний режим ---
    E32_SetMode(E32_MODE_NORMAL);
    HAL_Delay(50);   // маленька пауза для стабільності
}


void E32_EnableRSSI(void)
{
    uint8_t cmd[3] = {0xC1, 0xC1, 0xC1};
    uint8_t cfg[6];

    // === READ CONFIG ===
//    E32_SetDefaultConfig();
//    HAL_Delay(800);
//    E32_WaitAUX();

    E32_SetMode(E32_MODE_PROGRAM);
    HAL_Delay(80);
    E32_WaitAUX();

    HAL_UART_Transmit(&huart1, cmd, 3, HAL_MAX_DELAY);
    E32_WaitAUX();

    int len = HAL_UART_Receive(&huart1, cfg, 6, 200);
    printf("READ len=%d  cfg: %02X %02X %02X %02X %02X %02X\n",
            len, cfg[0], cfg[1], cfg[2], cfg[3], cfg[4], cfg[5]);

    if (len != 6)
    {
        printf("CONFIG READ ERROR!\n");
        return;
    }

    // === MODIFY CONFIG ===
    cfg[3] |= 0x01;   // RSSI enable

    // === WRITE CONFIG ===
    HAL_UART_Transmit(&huart1, cfg, 6, HAL_MAX_DELAY);
    E32_WaitAUX();   // wait write complete

    // === BACK TO NORMAL ===
    E32_SetMode(E32_MODE_NORMAL);
    HAL_Delay(50);
}


