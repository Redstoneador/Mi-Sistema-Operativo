#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include "kernel.h"

#define MAX_WAITING_TASKS 4

typedef struct {
    int32_t value;                         
    int8_t waiting_tasks[MAX_WAITING_TASKS];
    uint8_t waiting_count;                  
} semaphore_t;

void sem_init(semaphore_t *sem, int32_t initial_value);
void sem_wait(semaphore_t *sem);
void sem_signal(semaphore_t *sem);

#endif