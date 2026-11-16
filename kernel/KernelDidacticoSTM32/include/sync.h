#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include <stddef.h>
#include "kernel.h"

typedef struct {
    volatile int32_t value;
    uint8_t wait_queue[MAX_TASKS];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} semaphore_t;

void sem_init(semaphore_t *sem, int32_t initial_value);
void sem_wait(semaphore_t *sem);
void sem_signal(semaphore_t *sem);

typedef struct {
    semaphore_t sem;
    uint8_t owner_id;
    uint8_t locked;
} mutex_t;

void mutex_init(mutex_t *mtx);
void mutex_lock(mutex_t *mtx);
void mutex_unlock(mutex_t *mtx);

#define MSG_QUEUE_CAPACITY 8
typedef struct {
    uint32_t buffer[MSG_QUEUE_CAPACITY];
    uint8_t head;
    uint8_t tail;
    mutex_t lock;
    semaphore_t items_avail;
    semaphore_t space_avail;
} msg_queue_t;

void mq_init(msg_queue_t *mq);
void mq_send(msg_queue_t *mq, uint32_t data);
uint32_t mq_receive(msg_queue_t *mq);

#endif