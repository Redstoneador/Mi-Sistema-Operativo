#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

#define MAX_TASKS 6
#define STACK_SIZE 256
#define CANARY_VAL 0xDEADBEEF

typedef enum {
    TASK_NEW = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef enum {
    SCHED_ROUND_ROBIN = 0,
    SCHED_PRIORITY = 1
} scheduler_algorithm_t;

typedef struct {
    uint32_t *stack_top;
    void (*task_func)(void);
    uint32_t canary;
    uint32_t stack[STACK_SIZE] __attribute__((aligned(8)));
    uint8_t id;
    char name[12];
    task_state_t state;
    uint8_t priority;
    uint32_t wake_tick;
    uint32_t run_ticks;
    uint32_t wait_ticks;
} tcb_t;

typedef struct {
    uint8_t task_id;
    char name[12];
    task_state_t state;
    uint8_t priority;
    uint8_t stack_corrupted;
    uint32_t stack_free_words;
    uint32_t run_ticks;
    uint32_t wait_ticks;
} task_info_t;

// Núcleo y gestión de tareas
void kernel_init(void);
int os_create_task(const char *name, void (*task_func)(void), uint8_t priority);
void os_kill_task(uint8_t task_id);
void os_start(void);
void os_delay(uint32_t ms);
void os_yield(void);

// Planificación y estadísticas
void set_scheduler_algorithm(scheduler_algorithm_t algo);
scheduler_algorithm_t get_scheduler_algorithm(void);
void sys_get_task_stats(task_info_t *stats, uint8_t *count);
uint32_t os_get_uptime_ms(void);

// Memoria dinámica
void* os_malloc(size_t size);
void os_free(void *ptr);

#endif