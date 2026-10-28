#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int32_t OSStatus;
typedef uint32_t UInt32;
typedef int32_t SInt32;
typedef float Float32;
typedef double Float64;
#ifndef noErr
#define noErr 0
#endif

typedef struct __CFDictionary *CFMutableDictionaryRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef const void *CFAllocatorRef;

CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef allocator, long capacity, const void *keyCallBacks, const void *valueCallBacks);
const void *CFDictionaryGetValue(CFDictionaryRef theDict, const void *key);
void CFDictionarySetValue(CFMutableDictionaryRef theDict, const void *key, const void *value);
void CFDictionaryRemoveValue(CFMutableDictionaryRef theDict, const void *key);
