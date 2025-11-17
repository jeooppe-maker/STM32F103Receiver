#ifndef E32_H
#define E32_H

#include "main.h"
#include "ssd1306.h"

// --- Піни модуля E32 ---
#define E32_M0_PIN   GPIO_PIN_13
#define E32_M0_PORT  GPIOB
#define E32_M1_PIN   GPIO_PIN_12
#define E32_M1_PORT  GPIOB
#define E32_AUX_PIN  GPIO_PIN_14
#define E32_AUX_PORT GPIOB

// --- Режими роботи E32 ---
typedef enum {
    E32_MODE_NORMAL = 0,
    E32_MODE_WAKEUP,
    E32_MODE_POWERDOWN,
    E32_MODE_PROGRAM
} E32_Mode;

// --- Зовнішній UART, який використовується для E32 ---
extern UART_HandleTypeDef huart1;
// Зовнішній UART, який використовується для E32
extern UART_HandleTypeDef *E32_UART;

#define LORA_UART   (&huart2)
// --- Буфер для приймання даних ---
extern uint8_t LoRa_RX_Buffer[64];
#define RX_LINE_MAX 32
extern char rx_line[RX_LINE_MAX];
extern uint8_t rx_idx;


// --- Прототипи функцій ---
void E32_SetMode(E32_Mode mode);
uint8_t E32_IsReady(void);
void E32_SendString(char *str);
void E32_SendByte(uint8_t data);
uint8_t E32_ReadRSSI(void);
void E32_EnableRSSI(void);
#endif
