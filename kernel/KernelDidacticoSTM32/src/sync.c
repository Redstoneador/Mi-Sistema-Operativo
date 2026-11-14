#include "sync.h"

extern pcb_t task_list[MAX_TASKS];
extern int8_t current_task_index;

void sem_init(semaphore_t *sem, int32_t initial_value) {
    sem->value = initial_value;
    sem->waiting_count = 0;
    for (int i = 0; i < MAX_WAITING_TASKS; i++) {
        sem->waiting_tasks[i] = -1;
    }
}

void sem_wait(semaphore_t *sem) {
    __disable_irq();
    
    sem->value--;
    if (sem->value < 0) {
        task_list[current_task_index].state = TASK_BLOCKED;
        
        if (sem->waiting_count < MAX_WAITING_TASKS) {
            sem->waiting_tasks[sem->waiting_count++] = current_task_index;
        }
        
        __enable_irq();
        
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        
        while (task_list[current_task_index].state == TASK_BLOCKED) {
        }
    } else {
        __enable_irq();
    }
}

void sem_signal(semaphore_t *sem) {
    __disable_irq();
    
    sem->value++;
    if (sem->value <= 0) {
        if (sem->waiting_count > 0) {
            int8_t woken_task = sem->waiting_tasks[0];
            
            for (int i = 0; i < sem->waiting_count - 1; i++) {
                sem->waiting_tasks[i] = sem->waiting_tasks[i + 1];
            }
            sem->waiting_tasks[sem->waiting_count - 1] = -1;
            sem->waiting_count--;
            
            if (woken_task != -1) {
                task_list[woken_task].state = TASK_READY;
            }
        }
    }
    
    __enable_irq();
}