#include "kernel.h"
#include "stm32f1xx.h"

tcb_t task_list[MAX_TASKS];
int current_task_index = 0;
volatile tcb_t *current_tcb = NULL;
static scheduler_algorithm_t current_algo = SCHED_ROUND_ROBIN;
static volatile uint32_t system_ticks = 0;

// --- GESTOR DE MEMORIA DINÁMICA (HEAP PROPIO FIRST-FIT) ---
#define HEAP_SIZE 2048
static uint8_t rtos_heap[HEAP_SIZE] __attribute__((aligned(4)));

typedef struct block_header {
    size_t size;
    int free;
    struct block_header *next;
} block_header_t;

static block_header_t *heap_start = NULL;

void init_heap(void) {
    heap_start = (block_header_t*)rtos_heap;
    heap_start->size = HEAP_SIZE - sizeof(block_header_t);
    heap_start->free = 1;
    heap_start->next = NULL;
}

void* os_malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 3) & ~3; // Alinear a 4 bytes

    __disable_irq();
    block_header_t *current = heap_start;
    while (current != NULL) {
        if (current->free && current->size >= size) {
            if (current->size > size + sizeof(block_header_t)) {
                block_header_t *new_block = (block_header_t*)((uint8_t*)(current + 1) + size);
                new_block->size = current->size - size - sizeof(block_header_t);
                new_block->free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;
            }
            current->free = 0;
            __enable_irq();
            return (void*)(current + 1);
        }
        current = current->next;
    }
    __enable_irq();
    return NULL;
}

void os_free(void *ptr) {
    if (ptr == NULL) return;

    __disable_irq();
    block_header_t *header = (block_header_t*)ptr - 1;
    header->free = 1;

    // Coalescencia: fusionar bloques libres contiguos
    block_header_t *current = heap_start;
    while (current != NULL && current->next != NULL) {
        if (current->free && current->next->free) {
            current->size += sizeof(block_header_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
    __enable_irq();
}

void os_get_heap_stats(size_t *total, size_t *used, size_t *free_mem) {
    *total = HEAP_SIZE;
    *used = 0;
    *free_mem = 0;
    __disable_irq();
    block_header_t *curr = heap_start;
    while (curr != NULL) {
        if (curr->free) *free_mem += curr->size;
        else *used += curr->size;
        curr = curr->next;
    }
    __enable_irq();
}

// --- TAREA IDLE (Modo de bajo consumo) ---
static void idle_task(void) {
    while (1) {
        __WFI(); 
    }
}

// --- PLANIFICADOR DUAL ---
void set_scheduler_algorithm(scheduler_algorithm_t algo) {
    current_algo = algo;
}

scheduler_algorithm_t get_scheduler_algorithm(void) {
    return current_algo;
}

uint32_t os_get_uptime_ms(void) {
    return system_ticks;
}

void os_yield(void) {
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void HardFault_Handler(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    GPIOC->CRH &= ~(0xF << 20);
    GPIOC->CRH |=  (0x2 << 20);

    while (1) {
        GPIOC->ODR ^= (1U << 13);
        for (volatile uint32_t i = 0; i < 100000; i++);
    }
}

void SysTick_Handler(void) {
    system_ticks++;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].id != 0) {
            if (task_list[i].state == TASK_RUNNING) {
                task_list[i].run_ticks++;
            } else if (task_list[i].state == TASK_READY) {
                task_list[i].wait_ticks++;
            } else if (task_list[i].state == TASK_BLOCKED && task_list[i].wake_tick > 0) {
                if (system_ticks >= task_list[i].wake_tick) {
                    task_list[i].wake_tick = 0;
                    task_list[i].state = TASK_READY;
                }
            }
        }
    }

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void select_next_task(void) {
    if (current_tcb != NULL && current_tcb->canary != CANARY_VAL) {
        HardFault_Handler(); 
    }

    if (task_list[current_task_index].state == TASK_RUNNING) {
        task_list[current_task_index].state = TASK_READY;
    }

    if (current_algo == SCHED_ROUND_ROBIN) {
        int next = current_task_index;
        for (int i = 0; i < MAX_TASKS; i++) {
            next = (next + 1) % MAX_TASKS;
            if (task_list[next].state == TASK_READY && task_list[next].id != 0) {
                current_task_index = next;
                current_tcb = &task_list[next];
                task_list[next].state = TASK_RUNNING;
                return;
            }
        }
    } else {
        int best_task = -1;
        uint8_t highest_priority = 255;

        for (int i = 1; i <= MAX_TASKS; i++) {
            int idx = (current_task_index + i) % MAX_TASKS;
            if (task_list[idx].state == TASK_READY && task_list[idx].id != 0) {
                if (task_list[idx].priority < highest_priority) {
                    highest_priority = task_list[idx].priority;
                    best_task = idx;
                }
            }
        }

        if (best_task != -1) {
            current_task_index = best_task;
            current_tcb = &task_list[best_task];
            task_list[best_task].state = TASK_RUNNING;
        }
    }
}

__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        "cpsid i\n\t"
        "mrs r0, psp\n\t"
        "isb\n\t"

        "ldr r1, =current_tcb\n\t"
        "ldr r2, [r1]\n\t"

        "stmdb r0!, {r4-r11}\n\t"
        "str r0, [r2]\n\t"

        "stmdb sp!, {r1, lr}\n\t"
        "bl select_next_task\n\t"
        "ldmia sp!, {r1, lr}\n\t"

        "ldr r2, [r1]\n\t"
        "ldr r0, [r2]\n\t"

        "ldmia r0!, {r4-r11}\n\t"
        "msr psp, r0\n\t"
        "isb\n\t"
        "cpsie i\n\t"
        "bx lr\n\t"
        ::: "memory"
    );
}

void kernel_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_list[i].state = TASK_TERMINATED;
        task_list[i].id = 0;
        task_list[i].priority = 255;
        task_list[i].wake_tick = 0;
    }
    system_ticks = 0;
    current_task_index = 0;
    current_tcb = NULL;
    init_heap();

    os_create_task("idle", idle_task, 254);
}

int os_create_task(const char *name, void (*task_func)(void), uint8_t priority) {
    __disable_irq();
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].id == 0 || task_list[i].state == TASK_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        __enable_irq();
        return -1;
    }

    tcb_t *tcb = &task_list[slot];
    tcb->state = TASK_NEW;
    tcb->id = slot + 1;
    tcb->task_func = task_func;
    tcb->priority = priority;
    tcb->canary = CANARY_VAL;
    tcb->wake_tick = 0;
    tcb->run_ticks = 0;
    tcb->wait_ticks = 0;

    int j = 0;
    while (name && name[j] && j < 11) {
        tcb->name[j] = name[j];
        j++;
    }
    tcb->name[j] = '\0';

    for (int k = 0; k < STACK_SIZE; k++) {
        tcb->stack[k] = 0xA5A5A5A5UL;
    }

    uint32_t stack_addr = (uint32_t)(&tcb->stack[STACK_SIZE]);
    stack_addr &= ~0x07UL;
    uint32_t *sp = (uint32_t *)stack_addr;

    *(--sp) = 0x01000000UL;                     
    *(--sp) = ((uint32_t)task_func) | 0x01UL;   
    *(--sp) = 0xFFFFFFFDUL;                     
    *(--sp) = 0x12121212UL;                     
    *(--sp) = 0x03030303UL;                     
    *(--sp) = 0x02020202UL;                     
    *(--sp) = 0x01010101UL;                     
    *(--sp) = 0x00000000UL;                     

    *(--sp) = 0x11111111UL;
    *(--sp) = 0x10101010UL;
    *(--sp) = 0x09090909UL;
    *(--sp) = 0x08080808UL;
    *(--sp) = 0x07070707UL;
    *(--sp) = 0x06060606UL;
    *(--sp) = 0x05050505UL;
    *(--sp) = 0x04040404UL;

    tcb->stack_top = sp;
    tcb->state = TASK_READY; 
    __enable_irq();
    return tcb->id;
}

void os_kill_task(uint8_t task_id) {
    if (task_id <= 1 || task_id > MAX_TASKS) return; 
    __disable_irq();
    int idx = task_id - 1;
    if (task_list[idx].id == task_id) {
        task_list[idx].state = TASK_TERMINATED;
        task_list[idx].id = 0;
        if (idx == current_task_index) {
            SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
        }
    }
    __enable_irq();
}

void os_delay(uint32_t ms) {
    if (ms == 0) return;
    __disable_irq();
    current_tcb->wake_tick = system_ticks + ms;
    current_tcb->state = TASK_BLOCKED;
    __enable_irq();
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void sys_get_task_stats(task_info_t *stats, uint8_t *count) {
    uint8_t n = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].id != 0) {
            stats[n].task_id = task_list[i].id;
            for (int j = 0; j < 12; j++) stats[n].name[j] = task_list[i].name[j];
            stats[n].state = task_list[i].state;
            stats[n].priority = task_list[i].priority;
            stats[n].stack_corrupted = (task_list[i].canary != CANARY_VAL);
            stats[n].run_ticks = task_list[i].run_ticks;
            stats[n].wait_ticks = task_list[i].wait_ticks;

            uint32_t free_words = 0;
            while (free_words < STACK_SIZE && task_list[i].stack[free_words] == 0xA5A5A5A5UL) {
                free_words++;
            }
            stats[n].stack_free_words = free_words;
            n++;
        }
    }
    *count = n;
}

void os_start(void) {
    NVIC_SetPriority(PendSV_IRQn, 0xFF);
    NVIC_SetPriority(SysTick_IRQn, 0xFE);

    SysTick_Config(8000); 

    int start_idx = (task_list[1].id != 0) ? 1 : 0;
    current_task_index = start_idx;
    task_list[start_idx].state = TASK_RUNNING;
    current_tcb = &task_list[start_idx];

    __asm volatile (
        "ldr r1, =current_tcb\n\t"
        "ldr r2, [r1]\n\t"
        "ldr r0, [r2]\n\t"

        "ldmia r0!, {r4-r11}\n\t"
        "msr psp, r0\n\t"

        "movs r2, #2\n\t"
        "msr control, r2\n\t"
        "isb\n\t"

        "cpsie i\n\t"

        "pop {r0, r1, r2, r3}\n\t"
        "pop {r12}\n\t"
        "add sp, sp, #4\n\t"
        "pop {r0}\n\t"
        "add sp, sp, #4\n\t"
        "mov lr, #0xFFFFFFFF\n\t"
        "bx r0\n\t"
    );

    while (1);
}