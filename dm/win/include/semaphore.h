#pragma once
#include "dispatch/dispatch.h"

typedef dispatch_semaphore_t sem_t;

static inline int sem_init(sem_t *sem, int pshared, unsigned int value) {
    (void)pshared;
    if (!sem) return -1;
    *sem = dispatch_semaphore_create((long)value);
    return *sem ? 0 : -1;
}

static inline int sem_wait(sem_t *sem) {
    if (!sem || !*sem) return -1;
    return (int)dispatch_semaphore_wait(*sem, DISPATCH_TIME_FOREVER);
}

static inline int sem_post(sem_t *sem) {
    if (!sem || !*sem) return -1;
    dispatch_semaphore_signal(*sem);
    return 0;
}

static inline int sem_destroy(sem_t *sem) {
    if (!sem || !*sem) return -1;
    dispatch_release(*sem);
    *sem = 0;
    return 0;
}
