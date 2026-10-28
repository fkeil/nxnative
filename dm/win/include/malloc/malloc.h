#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct _malloc_zone_t {
    void *reserved;
} malloc_zone_t;

malloc_zone_t *malloc_create_zone(size_t start_size, unsigned flags);
void malloc_set_zone_name(malloc_zone_t *zone, const char *name);
void *malloc_zone_malloc(malloc_zone_t *zone, size_t size);
void *malloc_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size);
void *malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size);
void malloc_zone_free(malloc_zone_t *zone, void *ptr);
size_t malloc_size(const void *ptr);
size_t malloc_good_size(size_t size);
