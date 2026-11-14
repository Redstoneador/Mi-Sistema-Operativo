#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "stm32f103xb.h"

void uart1_init(uint32_t baudrate);
void uart_write_char(char c);
void uart_write_string(const char *str);

#endif