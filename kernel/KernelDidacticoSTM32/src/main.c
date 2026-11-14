#include "kernel.h"
#include "sync.h"
#include "uart.h"

void led_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(0xF << 20); 
    GPIOC->CRH |=  (0x2 << 20); 
}

#define BUFFER_SIZE 5
int buffer[BUFFER_SIZE];
int in = 0;
int out = 0;

semaphore_t mutex;
semaphore_t empty_slots;
semaphore_t full_slots;

void tarea_led(void) {
    led_init();
    
    while (1) {
        GPIOC->ODR &= ~(1U << 13); 
        os_delay(500);

        GPIOC->ODR |=  (1U << 13); 
        os_delay(500);
    }
}

void tarea_productor(void) {
    int item = 0;
    while (1) {
        item++;
        sem_wait(&empty_slots);
        sem_wait(&mutex);
        
        buffer[in] = item;
        in = (in + 1) % BUFFER_SIZE;
        
        sem_signal(&mutex);
        sem_signal(&full_slots);

        os_delay(300);
    }
}

void tarea_monitor(void) {
    uart1_init(9600); 
    uart_write_string("\r\n--- RTOS Kernel Iniciado ---\r\n");

    task_info_t stats[MAX_TASKS];

    while (1) {
        sys_get_task_stats(stats);

        uart_write_string("\r\n[ESTADISTICAS DE TAREAS]\r\n");
        for (int i = 0; i < MAX_TASKS; i++) {
            if (stats[i].task_id != 0) {
                uart_write_string("ID: ");
                uart_write_char('0' + (stats[i].task_id % 10));
                
                uart_write_string(" | Estado: ");
                uart_write_char('0' + stats[i].state);

                uart_write_string(" | Canary OK: ");
                uart_write_string(stats[i].stack_corrupted ? "NO (FAIL)" : "SI");
                uart_write_string("\r\n");
            }
        }

        os_delay(2000); 
    }
}

int main(void) {
    kernel_init();

    sem_init(&mutex, 1);
    sem_init(&empty_slots, BUFFER_SIZE);
    sem_init(&full_slots, 0);

    os_create_task(tarea_led, 1);       
    os_create_task(tarea_productor, 1); 
    os_create_task(tarea_monitor, 1);  

    os_start();

    while (1);
}