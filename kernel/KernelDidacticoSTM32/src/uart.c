#include "uart.h"
#include "sync.h"
#include "kernel.h"
#include "stm32f1xx.h"

extern volatile tcb_t *current_tcb;

static volatile char rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

static semaphore_t uart_rx_sem;
static mutex_t uart_tx_mtx;
static uint8_t uart_initialized = 0;

void uart1_init(uint32_t baudrate) {
    if (baudrate == 0) baudrate = 115200;

    // Habilitar reloj de GPIOA y USART1
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    // PA9 (TX) -> Alternate Function Push-Pull 50 MHz (0xB)
    GPIOA->CRH &= ~(0xF << 4);
    GPIOA->CRH |=  (0xB << 4);

    // PA10 (RX) -> Input Floating (0x4)
    GPIOA->CRH &= ~(0xF << 8);
    GPIOA->CRH |=  (0x4 << 8);

    // Configurar baudrate (PCLK2 = 8 MHz por defecto con HSI)
    USART1->BRR = 8000000UL / baudrate;

    // Inicializar primitivas de sincronización del driver
    sem_init(&uart_rx_sem, 0);
    mutex_init(&uart_tx_mtx);
    rx_head = 0;
    rx_tail = 0;

    // Habilitar TX, RX, interrupción por recepción (RXNEIE) y USART1
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    NVIC_SetPriority(USART1_IRQn, 5);
    NVIC_EnableIRQ(USART1_IRQn);

    uart_initialized = 1;
}

// Rutina de Servicio de Interrupción (ISR) para recepción por hardware
void USART1_IRQHandler(void) {
    uint32_t sr = USART1->SR;

    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        char c = (char)(USART1->DR & 0xFF);
        uint8_t next_head = (rx_head + 1) % UART_RX_BUFFER_SIZE;

        if (next_head != rx_tail) {
            rx_buffer[rx_head] = c;
            rx_head = next_head;
            sem_signal(&uart_rx_sem); 
        }
    }
}

void uart_write_char(char c) {
    uint32_t timeout = 100000;
    while (!(USART1->SR & USART_SR_TXE) && timeout > 0) {
        timeout--;
    }
    USART1->DR = (uint8_t)c;
}

void uart_write_string(const char *str) {
    if (str == NULL) return;

    if (uart_initialized && current_tcb != NULL) {
        mutex_lock(&uart_tx_mtx);
    }

    while (*str) {
        if (*str == '\n') {
            uart_write_char('\r'); 
        }
        uart_write_char(*str++);
    }

    if (uart_initialized && current_tcb != NULL) {
        mutex_unlock(&uart_tx_mtx);
    }
}

// IMPLEMENTACIÓN FALTANTE: Impresión de números sin requerir printf pesado
void uart_write_uint(uint32_t val) {
    char buf[12];
    int i = 0;

    if (val == 0) {
        uart_write_string("0");
        return;
    }

    while (val > 0 && i < 11) {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }

    char out[12];
    int j = 0;
    while (i > 0) {
        out[j++] = buf[--i];
    }
    out[j] = '\0';

    uart_write_string(out);
}

char uart_read_char(void) {
    sem_wait(&uart_rx_sem);

    __disable_irq();
    char c = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % UART_RX_BUFFER_SIZE;
    __enable_irq();

    return c;
}

int uart_data_available(void) {
    return (rx_head != rx_tail) ? 1 : 0;
}

// --- INTERFAZ UNIFORME DE DRIVER ---
static int uart1_drv_write(const uint8_t *buf, size_t len) {
    if (buf == NULL) return -1;
    if (uart_initialized && current_tcb != NULL) mutex_lock(&uart_tx_mtx);
    for (size_t i = 0; i < len; i++) {
        uart_write_char((char)buf[i]);
    }
    if (uart_initialized && current_tcb != NULL) mutex_unlock(&uart_tx_mtx);
    return (int)len;
}

static int uart1_drv_read(uint8_t *buf, size_t len) {
    if (buf == NULL) return -1;
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)uart_read_char();
    }
    return (int)len;
}

const char_device_t uart1_driver = {
    .name  = "uart1",
    .init  = uart1_init,
    .write = uart1_drv_write,
    .read  = uart1_drv_read
};