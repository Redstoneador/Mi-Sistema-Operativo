#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

#define UART_RX_BUFFER_SIZE 64

void uart1_init(uint32_t baudrate);
void uart_write_char(char c);
void uart_write_string(const char *str);
void uart_write_uint(uint32_t val);
char uart_read_char(void);
int uart_data_available(void);

typedef struct {
    const char *name;
    void (*init)(uint32_t config);
    int (*write)(const uint8_t *buf, size_t len);
    int (*read)(uint8_t *buf, size_t len);
} char_device_t;

extern const char_device_t uart1_driver;

#endif