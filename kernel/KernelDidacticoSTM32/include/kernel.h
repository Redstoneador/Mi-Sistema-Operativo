#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include "stm32f103xb.h"

typedef enum {
    TASK_NEW = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

#define STACK_SIZE   128   
#define MAX_TASKS    4   
#define STACK_CANARY 0xDEADBEEF 

typedef struct {
    uint32_t *stack_pointer;       
    uint32_t task_id;              
    task_state_t state;            
    uint8_t priority;              
    uint32_t stack[STACK_SIZE];    
    uint32_t delay_ticks;          
} pcb_t;

typedef struct {
    uint32_t task_id;
    task_state_t state;
    uint8_t priority;
    uint32_t delay_ticks;
    uint8_t stack_corrupted; 
} task_info_t;

extern pcb_t task_list[MAX_TASKS];
extern int8_t current_task_index;

void kernel_init(void);
int os_create_task(void (*task_func)(void), uint8_t priority);
void os_start(void);
void os_delay(uint32_t ticks);

uint32_t sys_get_tick(void);
uint32_t sys_get_pid(void);
void sys_get_task_stats(task_info_t *stats_out);

#endif