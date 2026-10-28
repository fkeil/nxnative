// libc passthrough table: C library functions consistent with AAPCS64 and with no struct layout dependency, connected directly to Apple libc. Goes into the swhle section.
#include "dm.h"
#include <math.h>
#include <ctype.h>
#include <time.h>
typedef struct { const char *name; void *fn; } hle_ent;
#define L(n) DM_SECT("swhle") static const hle_ent hle_lib_##n = { #n, (void *)n };
#define L2(n, fn) DM_SECT("swhle") static const hle_ent hle_lib_##n = { #n, (void *)(fn) };
L(memcpy) L(memset) L(memmove) L(memcmp) L(memchr) L(strlen) L(strnlen) L(strcmp) L(strncmp)
L(strcpy) L(strncpy) L(strcat) L(strncat) L(strchr) L(strrchr) L(strstr) L(strspn) L(strcspn)
#ifdef _WIN32
// Switch guest code is LP64 (64-bit long); Windows ARM64 is LLP64 (32-bit long, 64-bit long long)
L(strpbrk) L(strcasecmp) L(strncasecmp) L(atoi) L(atof) L2(strtol, strtoll) L2(strtoul, strtoull) L(strtoll) L(strtoull)
L(strtod) L(strtof) L(sqrtf) L(cbrtf) L(expf) L(acosf) L(cosf) L(sinf) L(atan2f) L(fmodf) L(cos)
L(sin) L(powf) L(atanf) L(tanf) L(asinf) L(log10) L(pow) L(logf) L(ldexpf) L(log10f) L(log)
L(ldexp) L(frexp) L(atan2) L(modff) L(exp2f) L(exp2) L(log2f) L(log2) L2(lroundf, llroundf)
#else
L(strpbrk) L(strcasecmp) L(strncasecmp) L(atoi) L(atof) L(strtol) L(strtoul) L(strtoll) L(strtoull)
L(strtod) L(strtof) L(sqrtf) L(cbrtf) L(expf) L(acosf) L(cosf) L(sinf) L(atan2f) L(fmodf) L(cos)
L(sin) L(powf) L(atanf) L(tanf) L(asinf) L(log10) L(pow) L(logf) L(ldexpf) L(log10f) L(log)
L(ldexp) L(frexp) L(atan2) L(modff) L(exp2f) L(exp2) L(log2f) L(log2) L(lroundf)
#endif
L(mktime) L(timegm) L(gmtime_r) L(localtime_r) L(strftime)
