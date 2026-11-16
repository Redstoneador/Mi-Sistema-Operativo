#include "stm32f1xx.h"
#include "kernel.h"
#include "uart.h"
#include "sync.h"

#define BUFFER_SIZE 5
static int buffer[BUFFER_SIZE];
static int head = 0;
static int tail = 0;
static volatile uint32_t total_produced = 0;
static volatile uint32_t total_consumed = 0;
static volatile uint8_t monitor_pc = 0;

semaphore_t mutex;
semaphore_t empty_slots;
semaphore_t full_slots;

#define CMD_BUFFER_SIZE 32
char cmd_buffer[CMD_BUFFER_SIZE];
int cmd_index = 0;

extern void os_get_heap_stats(size_t *total, size_t *used, size_t *free_mem);

/* =========================================================
 * INICIALIZACIÓN DE LOS 4 LEDS FÍSICOS (Demostración física)
 * ========================================================= */
void leds_init(void) {
    // Habilitar reloj para Puerto A y Puerto C
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN;

    // PC13: Salida Push-Pull 2 MHz (LED 1 - Integrado en Blue Pill)
    GPIOC->CRH &= ~(0xF << 20);
    GPIOC->CRH |=  (0x2 << 20);

    // PA0, PA1, PA2: Salida Push-Pull 2 MHz (LEDs 2, 3 y 4 - Externos)
    GPIOA->CRL &= ~(0xFFF << 0);
    GPIOA->CRL |=  (0x222 << 0);

    // Estado inicial (PC13 se apaga con nivel alto 1, PA0-PA2 se apagan con 0)
    GPIOC->BSRR = (1U << 13);
    GPIOA->BRR  = (1U << 0) | (1U << 1) | (1U << 2);
}

/* =========================================================
 * TAREAS DE APLICACIÓN (Concurrencia y Actuadores Físicos)
 * ========================================================= */
void tarea_led(void) {
    while (1) {
        GPIOC->ODR ^= (1U << 13); // Parpadeo LED 1 (PC13)
        os_delay(500);
    }
}

void tarea_productor(void) {
    int item = 100;
    while (1) {
        sem_wait(&empty_slots);
        sem_wait(&mutex);

        buffer[head] = item;
        head = (head + 1) % BUFFER_SIZE;
        total_produced++;

        // Activa físicamente el LED 2 (PA0) al producir un dato
        GPIOA->ODR ^= (1U << 0);

        if (monitor_pc) {
            uart_write_string("[PROD] Producido: ");
            uart_write_uint((uint32_t)item);
            uart_write_string("\r\n");
        }
        item++;

        sem_signal(&mutex);
        sem_signal(&full_slots);
        os_delay(800);
    }
}

void tarea_consumidor(void) {
    int consumed_item;
    while (1) {
        sem_wait(&full_slots);
        sem_wait(&mutex);

        consumed_item = buffer[tail];
        tail = (tail + 1) % BUFFER_SIZE;
        total_consumed++;

        // Activa físicamente el LED 3 (PA1) al consumir un dato
        GPIOA->ODR ^= (1U << 1);

        if (monitor_pc) {
            uart_write_string("  [CONS] Consumido: ");
            uart_write_uint((uint32_t)consumed_item);
            uart_write_string("\r\n");
        }

        sem_signal(&mutex);
        sem_signal(&empty_slots);

        os_delay(1200);
    }
}

void tarea_worker(void) {
    while (1) {
        // Activa LED 4 (PA2) periódicamente en tareas dinámicas
        GPIOA->ODR ^= (1U << 2);
        os_delay(2000);
    }
}

/* =========================================================
 * UTILIDADES E INTÉRPRETE DE COMANDOS DEL SHELL
 * ========================================================= */
int string_compare(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

uint32_t parse_uint(const char *str) {
    uint32_t res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (uint32_t)(*str - '0');
        str++;
    }
    return res;
}

void execute_command(char *cmd) {
    if (string_compare(cmd, "help") == 0) {
        uart_write_string("\r\n--- COMANDOS DEL KERNEL (REQUISITOS MINIMOS) ---\r\n");
        uart_write_string("  ps         - Lista PCBs, estados, pila y ticks de CPU/espera\r\n");
        uart_write_string("  kill <id>  - Termina una tarea por su ID\r\n");
        uart_write_string("  run        - Crea una tarea worker dinamica\r\n");
        uart_write_string("  mem        - Muestra estado del Heap y prueba malloc/free\r\n");
        uart_write_string("  uptime     - Tiempo encendido del sistema (ms)\r\n");
        uart_write_string("  sched <rr|prio> - Cambia planificador en vivo\r\n");
        uart_write_string("  pc         - Activa/desactiva log Productor-Consumidor\r\n");
        uart_write_string("  led        - Alterna LED PC13 manualmente\r\n");
        uart_write_string("  clear      - Limpia consola\r\n");
    }
    else if (string_compare(cmd, "ps") == 0 || string_compare(cmd, "tasks") == 0) {
        task_info_t stats[MAX_TASKS];
        uint8_t count = 0;
        sys_get_task_stats(stats, &count);

        uart_write_string("\r\nID | NOMBRE     | PRI | ESTADO  | RUN(ms) | WAIT(ms) | STACK_FREE\r\n");
        uart_write_string("-----------------------------------------------------------------\r\n");
        for (uint8_t i = 0; i < count; i++) {
            uart_write_uint(stats[i].task_id);
            uart_write_string("  | ");
            uart_write_string(stats[i].name);
            uart_write_string("\t| ");
            uart_write_uint(stats[i].priority);
            uart_write_string("   | ");

            if (stats[i].state == TASK_READY) uart_write_string("READY  ");
            else if (stats[i].state == TASK_RUNNING) uart_write_string("RUNNING");
            else if (stats[i].state == TASK_BLOCKED) uart_write_string("BLOCKED");
            else if (stats[i].state == TASK_NEW) uart_write_string("NEW    ");
            else uart_write_string("TERM   ");

            uart_write_string(" | ");
            uart_write_uint(stats[i].run_ticks);
            uart_write_string("\t | ");
            uart_write_uint(stats[i].wait_ticks);
            uart_write_string("\t  | ");
            uart_write_uint(stats[i].stack_free_words * 4);
            uart_write_string(" B");

            if (stats[i].stack_corrupted) {
                uart_write_string(" [OVERFLOW!]");
            }
            uart_write_string("\r\n");
        }
    }
    else if (starts_with(cmd, "kill ")) {
        uint8_t id = (uint8_t)parse_uint(cmd + 5);
        if (id <= 2) {
            uart_write_string("\r\n[SHELL] No se puede matar Idle (1) ni Shell (2).\r\n");
        } else {
            os_kill_task(id);
            uart_write_string("\r\n[SHELL] Tarea terminada. ID: ");
            uart_write_uint(id);
            uart_write_string("\r\n");
        }
    }
    else if (string_compare(cmd, "run") == 0) {
        int new_id = os_create_task("worker", tarea_worker, 4);
        if (new_id > 0) {
            uart_write_string("\r\n[SHELL] Tarea 'worker' creada con ID: ");
            uart_write_uint((uint32_t)new_id);
            uart_write_string("\r\n");
        } else {
            uart_write_string("\r\n[SHELL] Error: Maximo de tareas alcanzado.\r\n");
        }
    }
    else if (string_compare(cmd, "mem") == 0 || string_compare(cmd, "testm") == 0) {
        size_t total, used, free_mem;
        void *ptr1 = os_malloc(64);
        void *ptr2 = os_malloc(128);
        os_get_heap_stats(&total, &used, &free_mem);

        uart_write_string("\r\n[MEM] Heap Total: ");
        uart_write_uint((uint32_t)total);
        uart_write_string(" B | Usado: ");
        uart_write_uint((uint32_t)used);
        uart_write_string(" B | Libre: ");
        uart_write_uint((uint32_t)free_mem);
        uart_write_string(" B\r\n");

        if (ptr1) os_free(ptr1);
        if (ptr2) os_free(ptr2);
    }
    else if (string_compare(cmd, "uptime") == 0) {
        uint32_t ms = os_get_uptime_ms();
        uart_write_string("\r\n[SYS] Uptime: ");
        uart_write_uint(ms / 1000);
        uart_write_string(" s (");
        uart_write_uint(ms);
        uart_write_string(" ms)\r\n");
    }
    else if (string_compare(cmd, "sched rr") == 0) {
        set_scheduler_algorithm(SCHED_ROUND_ROBIN);
        uart_write_string("\r\n[SCHED] Planificador activo: ROUND-ROBIN\r\n");
    }
    else if (string_compare(cmd, "sched prio") == 0) {
        set_scheduler_algorithm(SCHED_PRIORITY);
        uart_write_string("\r\n[SCHED] Planificador activo: PRIORIDADES\r\n");
    }
    else if (string_compare(cmd, "pc") == 0) {
        monitor_pc = !monitor_pc;
        uart_write_string("\r\n[SYNC] Producidos: ");
        uart_write_uint(total_produced);
        uart_write_string(" | Consumidos: ");
        uart_write_uint(total_consumed);
        uart_write_string(monitor_pc ? " (Log ON)\r\n" : " (Log OFF)\r\n");
    }
    else if (string_compare(cmd, "led") == 0) {
        GPIOC->ODR ^= (1U << 13);
        uart_write_string("\r\n[GPIO] LED PC13 alternado.\r\n");
    }
    else if (string_compare(cmd, "clear") == 0) {
        uart_write_string("\033[2J\033[H");
    }
    else if (cmd[0] != '\0') {
        uart_write_string("\r\n[SHELL] Comando desconocido. Escribe 'help'.\r\n");
    }
}

void tarea_shell(void) {
    uart_write_string("\r\n========================================\r\n");
    uart_write_string("  Kernel Didactico STM32F1 - RTOS Propio\r\n");
    uart_write_string("  Escribe 'help' para ver comandos\r\n");
    uart_write_string("========================================\r\n> ");
    cmd_index = 0;
    cmd_buffer[0] = '\0';

    while (1) {
        char c = uart_read_char();

        if (c == '\r' || c == '\n') {
            uart_write_string("\r\n");
            cmd_buffer[cmd_index] = '\0';
            execute_command(cmd_buffer);
            cmd_index = 0;
            cmd_buffer[0] = '\0';
            uart_write_string("> ");
        }
        else if (c == 0x7F || c == '\b') {
            if (cmd_index > 0) {
                cmd_index--;
                cmd_buffer[cmd_index] = '\0';
                uart_write_string("\b \b");
            }
        }
        else if (cmd_index < CMD_BUFFER_SIZE - 1) {
            cmd_buffer[cmd_index++] = c;
            cmd_buffer[cmd_index] = '\0';
            uart_write_char(c);
        }
    }
}

int main(void) {
    SystemInit();
    uart1_init(115200);

    uart_write_string("\r\n[INIT] Inicializando Kernel RTOS...\r\n");
    kernel_init();

    // Inicializar los 4 LEDs físicos para la demostración
    leds_init();

    // Inicializar semáforos para el problema clásico Productor-Consumidor
    sem_init(&mutex, 1);
    sem_init(&empty_slots, BUFFER_SIZE);
    sem_init(&full_slots, 0);

    // Crear las 4 tareas concurrentes principales solicitadas
    os_create_task("shell", tarea_shell, 1);
    os_create_task("led",   tarea_led,   2);
    os_create_task("prod",  tarea_productor, 3);
    os_create_task("cons",  tarea_consumidor, 3);

    uart_write_string("[INIT] Arrancando Planificador...\r\n");
    os_start();

    while (1);
}