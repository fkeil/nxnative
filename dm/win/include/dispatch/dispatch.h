#pragma once
#include <stdint.h>

typedef void *dispatch_semaphore_t;
typedef uint64_t dispatch_time_t;

#define DISPATCH_TIME_NOW 0ull
#define DISPATCH_TIME_FOREVER (~0ull)
#define NSEC_PER_MSEC 1000000ull
#define NSEC_PER_SEC 1000000000ull

dispatch_semaphore_t dispatch_semaphore_create(long value);
long dispatch_semaphore_signal(dispatch_semaphore_t dsema);
long dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout);
void dispatch_release(void *object);
dispatch_time_t dispatch_time(dispatch_time_t when, int64_t delta);
