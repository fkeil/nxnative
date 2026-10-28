#pragma once
#include <stdint.h>
#include <stddef.h>

#define CC_SHA256_DIGEST_LENGTH 32
typedef uint32_t CC_LONG;

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t buffer[64];
} CC_SHA256_CTX;

int CC_SHA256_Init(CC_SHA256_CTX *c);
int CC_SHA256_Update(CC_SHA256_CTX *c, const void *data, CC_LONG len);
int CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *c);
unsigned char *CC_SHA256(const void *data, CC_LONG len, unsigned char *md);
