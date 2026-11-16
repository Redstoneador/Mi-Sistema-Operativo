#include "sync.h"
#include "kernel.h"
#include "stm32f1xx.h"

extern tcb_t task_list[MAX_TASKS];
extern int current_task_index;
extern volatile tcb_t *current_tcb;

/* =========================================================
 * 1. SEMÁFORO CONTADOR CON COLA DE ESPERA PROPIA
 * ========================================================= */
void sem_init(semaphore_t *sem, int32_t initial_value) {
    sem->value = initial_value;
    sem->head = 0;
    sem->tail = 0;
    sem->count = 0;
}

void sem_wait(semaphore_t *sem) {
    __disable_irq();

    if (sem->value > 0) {
        sem->value--;
        __enable_irq();
    } else {
        sem->wait_queue[sem->tail] = (uint8_t)current_task_index;
        sem->tail = (sem->tail + 1) % MAX_TASKS;
        sem->count++;

        task_list[current_task_index].state = TASK_BLOCKED;
        task_list[current_task_index].wake_tick = 0;

        __enable_irq();
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        __ISB();
    }
}

void sem_signal(semaphore_t *sem) {
    __disable_irq();

    if (sem->count > 0) {
        uint8_t task_idx = sem->wait_queue[sem->head];
        sem->head = (sem->head + 1) % MAX_TASKS;
        sem->count--;

        if (task_list[task_idx].id != 0 && task_list[task_idx].state == TASK_BLOCKED) {
            task_list[task_idx].state = TASK_READY;

            if (get_scheduler_algorithm() == SCHED_PRIORITY &&
                task_list[task_idx].priority < current_tcb->priority) {
                SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
            }
        } else {
            sem->value++;
        }
    } else {
        sem->value++;
    }

    __enable_irq();
}

/* =========================================================
 * 2. MUTEX (Exclusión Mutua con validación de propietario)
 * ========================================================= */
void mutex_init(mutex_t *mtx) {
    sem_init(&mtx->sem, 1);
    mtx->owner_id = 0;
    mtx->locked = 0;
}

void mutex_lock(mutex_t *mtx) {
    sem_wait(&mtx->sem);
    mtx->owner_id = current_tcb->id;
    mtx->locked = 1;
}

void mutex_unlock(mutex_t *mtx) {
    if (mtx->locked && mtx->owner_id == current_tcb->id) {
        mtx->owner_id = 0;
        mtx->locked = 0;
        sem_signal(&mtx->sem);
    }
}

/* =========================================================
 * 3. COLA DE MENSAJES / BUZÓN (Productor - Consumidor)
 * ========================================================= */
void mq_init(msg_queue_t *mq) {
    mq->head = 0;
    mq->tail = 0;
    mutex_init(&mq->lock);
    sem_init(&mq->items_avail, 0);
    sem_init(&mq->space_avail, MSG_QUEUE_CAPACITY);
}

void mq_send(msg_queue_t *mq, uint32_t data) {
    sem_wait(&mq->space_avail);
    mutex_lock(&mq->lock);

    mq->buffer[mq->tail] = data;
    mq->tail = (mq->tail + 1) % MSG_QUEUE_CAPACITY;

    mutex_unlock(&mq->lock);
    sem_signal(&mq->items_avail);
}

uint32_t mq_receive(msg_queue_t *mq) {
    uint32_t data;
    sem_wait(&mq->items_avail);
    mutex_lock(&mq->lock);

    data = mq->buffer[mq->head];
    mq->head = (mq->head + 1) % MSG_QUEUE_CAPACITY;

    mutex_unlock(&mq->lock);
    sem_signal(&mq->space_avail);
    return data;
}