#include "kernel.h"

pcb_t task_list[MAX_TASKS];
int8_t current_task_index = -1;

static int8_t next_task_index = 0;
static uint32_t next_task_id = 1;
static volatile uint32_t system_ticks = 0; 

uint32_t current_psp = 0;
uint32_t next_psp = 0;

void kernel_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_list[i].state = TASK_TERMINATED;
        task_list[i].task_id = 0;
        task_list[i].delay_ticks = 0;
    }
    current_task_index = -1;
    next_task_index = 0;
    system_ticks = 0;
}

int os_create_task(void (*task_func)(void), uint8_t priority) {
    int idx = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].state == TASK_TERMINATED) {
            idx = i;
            break;
        }
    }
    
    if (idx == -1) return -1;

    pcb_t *new_task = &task_list[idx];
    new_task->task_id = next_task_id++;
    new_task->state = TASK_READY;
    new_task->priority = priority;
    new_task->delay_ticks = 0;

    new_task->stack[0] = STACK_CANARY;

    uint32_t *sp = &new_task->stack[STACK_SIZE - 1];

    *(--sp) = (1U << 24);              
    *(--sp) = (uint32_t)task_func;     
    *(--sp) = 0x0e0e0e0e;              
    *(--sp) = 0x12121212;              
    *(--sp) = 0x03030303;              
    *(--sp) = 0x02020202;              
    *(--sp) = 0x01010101;             
    *(--sp) = 0x00000000;              

    for (int i = 0; i < 8; i++) {
        *(--sp) = 0x00000000;          
    }

    new_task->stack_pointer = sp;
    return 0;
}

__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        "mrs r0, psp\n\t"
        "isb\n\t"
        "stmdb r0!, {r4-r11}\n\t"
        
        "ldr r1, =current_psp\n\t"
        "ldr r2, [r1]\n\t"
        "str r0, [r2]\n\t"

        "push {lr}\n\t"
        "bl scheduler\n\t"
        "pop {lr}\n\t"

        "ldr r1, =next_psp\n\t"
        "ldr r2, [r1]\n\t"
        "ldr r0, [r2]\n\t"

        "ldmia r0!, {r4-r11}\n\t"
        "msr psp, r0\n\t"
        "isb\n\t"
        "bx lr\n\t"
    );
}

void scheduler(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].state != TASK_TERMINATED) {
            if (task_list[i].stack[0] != STACK_CANARY) {
                task_list[i].state = TASK_TERMINATED;
            }
        }
    }

    int checked = 0;
    while (checked < MAX_TASKS) {
        next_task_index = (next_task_index + 1) % MAX_TASKS;
        checked++;
        if (task_list[next_task_index].state == TASK_READY) {
            break;
        }
    }

    if (current_task_index != -1) {
        if (task_list[current_task_index].state == TASK_RUNNING) {
            task_list[current_task_index].state = TASK_READY;
        }
    }

    if (task_list[next_task_index].state == TASK_READY) {
        current_task_index = next_task_index;
        task_list[current_task_index].state = TASK_RUNNING;
    }

    current_psp = (uint32_t)&task_list[current_task_index].stack_pointer;
    next_psp = (uint32_t)&task_list[next_task_index].stack_pointer;
}

void SysTick_Handler(void) {
    system_ticks++;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].state == TASK_BLOCKED && task_list[i].delay_ticks > 0) {
            task_list[i].delay_ticks--;
            if (task_list[i].delay_ticks == 0) {
                task_list[i].state = TASK_READY;
            }
        }
    }

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; 
}

void os_delay(uint32_t ticks) {
    if (ticks == 0) return;
    
    __disable_irq();
    task_list[current_task_index].delay_ticks = ticks;
    task_list[current_task_index].state = TASK_BLOCKED;
    __enable_irq();

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    while (task_list[current_task_index].state == TASK_BLOCKED) {
        __WFI();
    }
}


void svc_handler_c(uint32_t *svc_args) {
    uint32_t pc = svc_args[6];
    uint8_t svc_number = ((uint8_t *)pc)[-2];

    switch (svc_number) {
        case 0: 
            svc_args[0] = system_ticks;
            break;
        case 1: 
            svc_args[0] = (current_task_index != -1) ? task_list[current_task_index].task_id : 0;
            break;
        case 2: 
            {
                task_info_t *user_buf = (task_info_t *)svc_args[0];
                for (int i = 0; i < MAX_TASKS; i++) {
                    user_buf[i].task_id = task_list[i].task_id;
                    user_buf[i].state = task_list[i].state;
                    user_buf[i].priority = task_list[i].priority;
                    user_buf[i].delay_ticks = task_list[i].delay_ticks;
                    user_buf[i].stack_corrupted = (task_list[i].stack[0] != STACK_CANARY) ? 1 : 0;
                }
            }
            break;
        default:
            break;
    }
}

__attribute__((naked)) void SVC_Handler(void) {
    __asm volatile (
        "tst lr, #4\n\t"
        "ite eq\n\t"
        "mrseq r0, msp\n\t"
        "mrsne r0, psp\n\t"
        "b svc_handler_c\n\t"
    );
}

uint32_t sys_get_tick(void) {
    uint32_t ret;
    __asm volatile ("svc #0\n\t" : "=r" (ret));
    return ret;
}

uint32_t sys_get_pid(void) {
    uint32_t ret;
    __asm volatile ("svc #1\n\t" : "=r" (ret));
    return ret;
}

void sys_get_task_stats(task_info_t *stats_out) {
    __asm volatile ("svc #2\n\t" : : "r" (stats_out) : "memory");
}

void os_start(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].state == TASK_READY) {
            current_task_index = i;
            break;
        }
    }

    if (current_task_index == -1) return;

    task_list[current_task_index].state = TASK_RUNNING;
    current_psp = (uint32_t)&task_list[current_task_index].stack_pointer;

    __asm volatile("ldr r0, =current_psp\n\t"
                   "ldr r1, [r0]\n\t"
                   "ldr r2, [r1]\n\t"
                   "ldmia r2!, {r4-r11}\n\t"
                   "msr psp, r2\n\t"
                   "mov r0, #2\n\t"
                   "msr control, r0\n\t"
                   "isb\n\t");

    SysTick->LOAD = (72000000UL / 1000UL) - 1UL;
    SysTick->VAL  = 0UL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | 
                    SysTick_CTRL_TICKINT_Msk   | 
                    SysTick_CTRL_ENABLE_Msk;

    uint32_t first_pc = task_list[current_task_index].stack[STACK_SIZE - 2];
    ((void(*)(void))first_pc)();
}