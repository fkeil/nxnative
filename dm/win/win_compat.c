#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <malloc.h>

// Undefine Windows macros that could collide with POSIX names
#undef min
#undef max
#undef d_type

#include "include/win_posix.h"
#include "include/CommonCrypto/CommonDigest.h"
#include "include/malloc/malloc.h"
#include "include/dispatch/dispatch.h"
#include "include/CoreFoundation/CoreFoundation.h"

__attribute__((constructor)) static void win_binary_fmode_init(void) {
    _set_fmode(_O_BINARY);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}

// ---------- PE section lookup (Mach-O getsectiondata equivalent) ----------
int _mh_execute_header = 0;
extern IMAGE_DOS_HEADER __ImageBase;

const uint8_t *getsectiondata(const void *mhp, const char *segname, const char *sectname, unsigned long *size) {
    (void)mhp; (void)segname;
    if (size) *size = 0;
    if (!sectname) return NULL;
    const uint8_t *base = (const uint8_t *)&__ImageBase;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + __ImageBase.e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (strncmp((const char *)sec[i].Name, sectname, 8) == 0) {
            if (size) *size = sec[i].Misc.VirtualSize;
            return base + sec[i].VirtualAddress;
        }
    }
    return NULL;
}

// ---------- Memory management (mmap / munmap / mprotect / msync / mincore) ----------
static DWORD prot_to_win(int prot) {
    if (prot == PROT_NONE) return PAGE_NOACCESS;
    int r = (prot & PROT_READ) != 0;
    int w = (prot & PROT_WRITE) != 0;
    int x = (prot & PROT_EXEC) != 0;
    if (x && w) return PAGE_EXECUTE_READWRITE;
    if (x) return PAGE_EXECUTE_READ;
    if (w) return PAGE_READWRITE;
    if (r) return PAGE_READONLY;
    return PAGE_NOACCESS;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t offset) {
    if (!len) return MAP_FAILED;
    DWORD wprot = prot_to_win(prot);
    void *p = NULL;
    if (flags & MAP_FIXED) {
        if (prot == PROT_NONE) {
            DWORD old = 0;
            if (VirtualProtect(addr, len, PAGE_NOACCESS, &old)) p = addr;
        } else {
            p = VirtualAlloc(addr, len, MEM_COMMIT, wprot);
            if (!p) {
                DWORD old = 0;
                if (VirtualProtect(addr, len, wprot, &old)) p = addr;
            }
        }
    } else {
        DWORD fl = (prot == PROT_NONE) ? MEM_RESERVE : (MEM_RESERVE | MEM_COMMIT);
        p = VirtualAlloc(addr, len, fl, (prot == PROT_NONE) ? PAGE_NOACCESS : wprot);
    }
    if (!p) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    if (fd >= 0) {
        if (!(prot & PROT_WRITE)) {
            DWORD old = 0;
            VirtualProtect(p, len, PAGE_READWRITE, &old);
        }
        pread(fd, p, len, offset);
        if (!(prot & PROT_WRITE)) {
            DWORD old = 0;
            VirtualProtect(p, len, wprot, &old);
        }
    }
    return p;
}

int munmap(void *addr, size_t len) {
    (void)len;
    if (!addr) return 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof mbi) && mbi.AllocationBase == addr) {
        if (VirtualFree(addr, 0, MEM_RELEASE)) return 0;
    }
    VirtualFree(addr, len, MEM_DECOMMIT);
    return 0;
}

int mprotect(void *addr, size_t len, int prot) {
    if (!len) return 0;
    DWORD wprot = prot_to_win(prot);
    if (prot == PROT_NONE) {
        if (VirtualFree(addr, len, MEM_DECOMMIT)) return 0;
    } else {
        // Ensure pages inside a reserved region (such as guest AS) are committed first
        if (VirtualAlloc(addr, len, MEM_COMMIT, wprot)) {
            if (prot & PROT_EXEC) FlushInstructionCache(GetCurrentProcess(), addr, len);
            return 0;
        }
    }
    DWORD old = 0;
    if (VirtualProtect(addr, len, wprot, &old)) {
        if (prot & PROT_EXEC) FlushInstructionCache(GetCurrentProcess(), addr, len);
        return 0;
    }
    errno = EACCES;
    return -1;
}

int msync(void *addr, size_t len, int flags) {
    (void)addr; (void)len; (void)flags;
    return 0;
}

int mincore(const void *addr, size_t len, char *vec) {
    size_t pg = 4096;
    size_t np = (len + pg - 1) / pg;
    if (vec && np) memset(vec, 1, np);
    (void)addr;
    return 0;
}

void sys_icache_invalidate(void *start, size_t len) {
    FlushInstructionCache(GetCurrentProcess(), start, len);
}

// ---------- Clocks and sleeping ----------
uint64_t dm_win_qpc_ns(void) {
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    uint64_t q = (uint64_t)cnt.QuadPart / (uint64_t)freq.QuadPart;
    uint64_t r = (uint64_t)cnt.QuadPart % (uint64_t)freq.QuadPart;
    return q * 1000000000ull + (r * 1000000000ull) / (uint64_t)freq.QuadPart;
}

uint64_t mach_absolute_time(void) {
    return dm_win_qpc_ns();
}

int clock_gettime(int clk_id, struct timespec *tp) {
    if (!tp) return -1;
    if (clk_id == CLOCK_REALTIME) {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        t -= 116444736000000000ull; // 1601 -> 1970 in 100ns units
        tp->tv_sec = (time_t)(t / 10000000ull);
        tp->tv_nsec = (long)((t % 10000000ull) * 100ull);
        return 0;
    }
    uint64_t ns = clock_gettime_nsec_np(clk_id);
    tp->tv_sec = (time_t)(ns / 1000000000ull);
    tp->tv_nsec = (long)(ns % 1000000000ull);
    return 0;
}

uint64_t clock_gettime_nsec_np(int clk_id) {
    if (clk_id == CLOCK_THREAD_CPUTIME_ID) {
        FILETIME create_ft, exit_ft, kern_ft, user_ft;
        if (GetThreadTimes(GetCurrentThread(), &create_ft, &exit_ft, &kern_ft, &user_ft)) {
            uint64_t k = ((uint64_t)kern_ft.dwHighDateTime << 32) | kern_ft.dwLowDateTime;
            uint64_t u = ((uint64_t)user_ft.dwHighDateTime << 32) | user_ft.dwLowDateTime;
            return (k + u) * 100ull;
        }
    }
    return dm_win_qpc_ns();
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return -1;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tv->tv_sec = (long)ts.tv_sec;
    tv->tv_usec = (long)(ts.tv_nsec / 1000);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (!req) return 0;
    uint64_t ns = (uint64_t)req->tv_sec * 1000000000ull + (uint64_t)req->tv_nsec;
    if (ns >= 1000000ull) {
        Sleep((DWORD)(ns / 1000000ull));
    } else if (ns > 0) {
        SwitchToThread();
    }
    return 0;
}

int usleep(useconds_t usec) {
    struct timespec ts = { (time_t)(usec / 1000000u), (long)((usec % 1000000u) * 1000u) };
    return nanosleep(&ts, NULL);
}

unsigned int sleep(unsigned int seconds) {
    Sleep(seconds * 1000u);
    return 0;
}

int mach_wait_until(uint64_t deadline) {
    uint64_t now = dm_win_qpc_ns();
    if (deadline > now) {
        uint64_t diff = deadline - now;
        struct timespec ts = { (time_t)(diff / 1000000000ull), (long)(diff % 1000000000ull) };
        nanosleep(&ts, NULL);
    }
    return 0;
}

int sched_yield(void) {
    SwitchToThread();
    return 0;
}

// ---------- File I/O and environment ----------
ssize_t pread(int fd, void *buf, size_t count, int64_t offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    uint8_t *dst = (uint8_t *)buf;
    size_t total = 0;
    while (total < count) {
        OVERLAPPED ov;
        memset(&ov, 0, sizeof ov);
        uint64_t pos = (uint64_t)offset + total;
        ov.Offset = (DWORD)(pos & 0xffffffffu);
        ov.OffsetHigh = (DWORD)(pos >> 32);
        DWORD chunk = (count - total) > 0x40000000u ? 0x40000000u : (DWORD)(count - total);
        DWORD got = 0;
        if (!ReadFile(h, dst + total, chunk, &got, &ov)) {
            DWORD err = GetLastError();
            if (err == ERROR_HANDLE_EOF) break;
            errno = (err == ERROR_NOACCESS) ? EFAULT : EIO;
            return total ? (ssize_t)total : -1;
        }
        if (!got) break;
        total += got;
    }
    return (ssize_t)total;
}

ssize_t pwrite(int fd, const void *buf, size_t count, int64_t offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    const uint8_t *src = (const uint8_t *)buf;
    size_t total = 0;
    while (total < count) {
        OVERLAPPED ov;
        memset(&ov, 0, sizeof ov);
        uint64_t pos = (uint64_t)offset + total;
        ov.Offset = (DWORD)(pos & 0xffffffffu);
        ov.OffsetHigh = (DWORD)(pos >> 32);
        DWORD chunk = (count - total) > 0x40000000u ? 0x40000000u : (DWORD)(count - total);
        DWORD put = 0;
        if (!WriteFile(h, src + total, chunk, &put, &ov)) {
            errno = EIO;
            return total ? (ssize_t)total : -1;
        }
        if (!put) break;
        total += put;
    }
    return (ssize_t)total;
}

int ftruncate(int fd, int64_t length) {
    return _chsize_s(fd, length) == 0 ? 0 : -1;
}

int truncate(const char *path, int64_t length) {
    int fd = _open(path, _O_RDWR | _O_BINARY);
    if (fd < 0) return -1;
    int r = ftruncate(fd, length);
    _close(fd);
    return r;
}

int fcntl(int fd, int cmd, ...) {
    (void)fd; (void)cmd;
    return 0;
}

int lstat(const char *path, struct stat *st) {
    return stat(path, st);
}

static const char *dirfd_base_path(int dfd);
static void resolve_at_path(int dfd, const char *path, char *out, size_t out_sz) {
    const char *base = (path && path[0] != '/' && path[0] != '\\' && !(path[0] && path[1] == ':')) ? dirfd_base_path(dfd) : NULL;
    if (base) snprintf(out, out_sz, "%s/%s", base, path);
    else strlcpy(out, path ? path : "", out_sz);
}

int fstatat(int dfd, const char *path, struct stat *st, int flags) {
    (void)flags;
    char full[1280];
    resolve_at_path(dfd, path, full, sizeof full);
    return stat(full, st);
}

int unlinkat(int dfd, const char *path, int flags) {
    char full[1280];
    resolve_at_path(dfd, path, full, sizeof full);
    if (flags & AT_REMOVEDIR) return _rmdir(full);
    return _unlink(full);
}

int mkdirat(int dfd, const char *path, mode_t mode) {
    (void)mode;
    char full[1280];
    resolve_at_path(dfd, path, full, sizeof full);
    return _mkdir(full);
}

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    char oldfull[1280], newfull[1280];
    resolve_at_path(olddirfd, oldpath, oldfull, sizeof oldfull);
    resolve_at_path(newdirfd, newpath, newfull, sizeof newfull);
    return MoveFileExA(oldfull, newfull, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

int renamex_np(const char *from, const char *to, unsigned int flags) {
    if (flags & RENAME_SWAP) {
        char tmp[1280];
        snprintf(tmp, sizeof tmp, "%s.swap_%lu", to, (unsigned long)GetCurrentProcessId());
        if (!MoveFileExA(to, tmp, MOVEFILE_REPLACE_EXISTING)) { errno = EIO; return -1; }
        if (!MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
            MoveFileExA(tmp, to, MOVEFILE_REPLACE_EXISTING);
            errno = EIO;
            return -1;
        }
        MoveFileExA(tmp, from, MOVEFILE_REPLACE_EXISTING);
        return 0;
    }
    DWORD wf = (flags & RENAME_EXCL) ? 0 : MOVEFILE_REPLACE_EXISTING;
    if (MoveFileExA(from, to, wf)) return 0;
    errno = (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS) ? EEXIST : EIO;
    return -1;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    errno = EINVAL;
    return -1;
}

int symlink(const char *target, const char *linkpath) {
    (void)target; (void)linkpath;
    errno = ENOSYS;
    return -1;
}

ssize_t getxattr(const char *path, const char *name, void *value, size_t size, uint32_t position, int options) {
    (void)path; (void)name; (void)value; (void)size; (void)position; (void)options;
    errno = ENOATTR;
    return -1;
}

int setxattr(const char *path, const char *name, const void *value, size_t size, uint32_t position, int options) {
    (void)path; (void)name; (void)value; (void)size; (void)position; (void)options;
    return 0;
}

int removexattr(const char *path, const char *name, int options) {
    (void)path; (void)name; (void)options;
    return 0;
}

size_t strlcpy(char *dst, const char *src, size_t siz) {
    size_t slen = strlen(src);
    if (siz) {
        size_t n = slen >= siz ? siz - 1 : slen;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return slen;
}

size_t strlcat(char *dst, const char *src, size_t siz) {
    size_t dlen = strnlen(dst, siz);
    size_t slen = strlen(src);
    if (dlen == siz) return siz + slen;
    if (slen < siz - dlen) {
        memcpy(dst + dlen, src, slen + 1);
    } else {
        memcpy(dst + dlen, src, siz - dlen - 1);
        dst[siz - 1] = '\0';
    }
    return dlen + slen;
}

void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen) {
    if (!needlelen) return (void *)haystack;
    if (haystacklen < needlelen || !haystack || !needle) return NULL;
    const uint8_t *h = (const uint8_t *)haystack;
    const uint8_t *n = (const uint8_t *)needle;
    uint8_t first = n[0];
    size_t limit = haystacklen - needlelen;
    for (size_t i = 0; i <= limit; i++) {
        if (h[i] == first && memcmp(h + i, n, needlelen) == 0)
            return (void *)(h + i);
    }
    return NULL;
}

static SRWLOCK g_memalign_lock = SRWLOCK_INIT;
static struct { void *aligned; void *raw; } g_memalign_tab[64];
static volatile LONG g_memalign_cnt = 0;

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr || alignment == 0 || (alignment & (alignment - 1)) != 0 || (alignment % sizeof(void *)) != 0)
        return EINVAL;
    if (alignment <= 16) {
        void *p = malloc(size ? size : 1);
        if (!p) return ENOMEM;
        *memptr = p;
        return 0;
    }
    void *raw = malloc((size ? size : 1) + alignment);
    if (!raw) return ENOMEM;
    void *aligned = (void *)(((uintptr_t)raw + alignment - 1) & ~(alignment - 1));
    AcquireSRWLockExclusive(&g_memalign_lock);
    for (int i = 0; i < 64; i++) {
        if (!g_memalign_tab[i].aligned) {
            g_memalign_tab[i].aligned = aligned;
            g_memalign_tab[i].raw = raw;
            InterlockedIncrement(&g_memalign_cnt);
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_memalign_lock);
    *memptr = aligned;
    return 0;
}

void dm_win_free(void *p) {
    if (!p) return;
    if (g_memalign_cnt > 0) {
        void *raw = NULL;
        AcquireSRWLockExclusive(&g_memalign_lock);
        for (int i = 0; i < 64; i++) {
            if (g_memalign_tab[i].aligned == p) {
                raw = g_memalign_tab[i].raw;
                g_memalign_tab[i].aligned = NULL;
                g_memalign_tab[i].raw = NULL;
                InterlockedDecrement(&g_memalign_cnt);
                break;
            }
        }
        ReleaseSRWLockExclusive(&g_memalign_lock);
        if (raw) {
            (free)(raw);
            return;
        }
    }
    (free)(p);
}

void arc4random_buf(void *buf, size_t nbytes) {
    uint8_t *p = (uint8_t *)buf;
    static uint64_t s = 0x853c49e6748fea9bull;
    if (s == 0x853c49e6748fea9bull) s ^= dm_win_qpc_ns();
    for (size_t i = 0; i < nbytes; i++) {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        uint64_t r = s * 0x2545f4914f6cdd1dull;
        p[i] = (uint8_t)(r >> 56);
    }
}

uint32_t arc4random(void) {
    uint32_t v = 0;
    arc4random_buf(&v, sizeof v);
    return v;
}

char *tzname[2] = { "UTC", "UTC" };

void dm_win_tzset(void) {
    _tzset();
    if (_tzname[0] && *_tzname[0]) tzname[0] = _tzname[0];
    if (_tzname[1] && *_tzname[1]) tzname[1] = _tzname[1];
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    if (!result || !timep) return NULL;
    memset(result, 0, sizeof *result);
    if (gmtime_s((void *)result, timep) != 0) return NULL;
    result->tm_gmtoff = 0;
    result->tm_zone = "UTC";
    return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    if (!result || !timep) return NULL;
    memset(result, 0, sizeof *result);
    dm_win_tzset();
    if (localtime_s((void *)result, timep) != 0) return NULL;
    long tz_sec = 0;
    _get_timezone(&tz_sec);
    long dst_bias = 0;
    if (result->tm_isdst > 0) _get_dstbias(&dst_bias);
    result->tm_gmtoff = -(int64_t)(tz_sec + dst_bias);
    int idx = (result->tm_isdst > 0) ? 1 : 0;
    result->tm_zone = (tzname[idx] && *tzname[idx]) ? tzname[idx] : "UTC";
    return result;
}

struct tm *dm_win_localtime(const time_t *timep) {
    static __thread struct tm t_tm;
    return localtime_r(timep, &t_tm);
}

time_t dm_win_mktime(struct tm *tm_p) {
    if (!tm_p) return (time_t)-1;
    return (time_t)_mktime64((void *)tm_p);
}

#undef strftime
size_t dm_win_strftime(char *s, size_t max, const char *format, const struct tm *tm_p) {
    if (!s || !format || !tm_p) return 0;
    return strftime(s, max, format, (const void *)tm_p);
}

time_t timegm(struct tm *tm) {
    return _mkgmtime((void *)tm);
}

int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    (void)name; (void)namelen; (void)oldp; (void)oldlenp; (void)newp; (void)newlen;
    return -1;
}

int madvise(void *addr, size_t len, int advice) {
    (void)addr; (void)len; (void)advice;
    return 0;
}

int sigaltstack(const stack_t *ss, stack_t *oss) { (void)ss; (void)oss; return 0; }
int sigemptyset(sigset_t *set) { if (set) *set = 0; return 0; }
int sigaddset(sigset_t *set, int signo) { if (set) *set |= (1u << (signo & 31)); return 0; }
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset) { (void)how; (void)set; if (oset) *oset = 0; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) { if (attr) attr->detachstate = detachstate; return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t *attr) { if (attr) attr->type = 0; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) { (void)attr; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) { if (attr) attr->type = type; return 0; }
int pthread_key_delete(pthread_key_t key) { (void)key; return 0; }
void *dlopen(const char *path, int mode) { (void)mode; return path ? (void *)LoadLibraryA(path) : (void *)GetModuleHandleA(NULL); }
int dlclose(void *handle) { (void)handle; return 0; }
char *dlerror(void) { return NULL; }

int fsync(int fd) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    FlushFileBuffers(h);
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value ? value : "") == 0 ? 0 : -1;
}

int unsetenv(const char *name) {
    return _putenv_s(name, "") == 0 ? 0 : -1;
}

int getpagesize(void) {
    return 16384; // Keep 16 KiB granularity expected by loader.c / hle_svc.c
}

long sysconf(int name) {
    if (name == _SC_NPROCESSORS_ONLN) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return (long)si.dwNumberOfProcessors;
    }
    return 16384;
}

int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    (void)name; (void)oldp; (void)oldlenp; (void)newp; (void)newlen;
    return -1;
}

int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) return -1;
    memset(usage, 0, sizeof *usage);
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        uint64_t ku = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) / 10ull;
        uint64_t uu = (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime) / 10ull;
        usage->ru_stime.tv_sec = (long)(ku / 1000000ull);
        usage->ru_stime.tv_usec = (long)(ku % 1000000ull);
        usage->ru_utime.tv_sec = (long)(uu / 1000000ull);
        usage->ru_utime.tv_usec = (long)(uu % 1000000ull);
    }
    return 0;
}

int clonefile(const char *src, const char *dst, int flags) {
    (void)flags;
    DWORD attr = GetFileAttributesA(src);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        errno = ENOENT;
        return -1;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        if (!CreateDirectoryA(dst, NULL)) {
            errno = (GetLastError() == ERROR_ALREADY_EXISTS) ? EEXIST : EIO;
            return -1;
        }
        char pat[1280];
        snprintf(pat, sizeof pat, "%s\\*", src);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;
        int rc = 0;
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char sp[1280], dp[1280];
            snprintf(sp, sizeof sp, "%s/%s", src, fd.cFileName);
            snprintf(dp, sizeof dp, "%s/%s", dst, fd.cFileName);
            if (clonefile(sp, dp, flags) != 0) { rc = -1; break; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        return rc;
    }
    if (CopyFileA(src, dst, TRUE)) return 0;
    errno = (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) ? EEXIST : EIO;
    return -1;
}

int statfs(const char *path, struct statfs *buf) {
    (void)path;
    if (!buf) return -1;
    memset(buf, 0, sizeof *buf);
    buf->f_bsize = 4096;
    buf->f_blocks = 10000000;
    buf->f_bfree = 5000000;
    buf->f_bavail = 5000000;
    return 0;
}

int fstatfs(int fd, struct statfs *buf) {
    (void)fd;
    return statfs(".", buf);
}

int dm_win_mkdir(const char *path, int mode) {
    (void)mode;
    return _mkdir(path);
}

// ---------- Directory enumeration (opendir / readdir / closedir / dirfd) ----------
struct DIR {
    HANDLE h;
    WIN32_FIND_DATAA fd;
    struct dirent ent;
    int first;
    int slot;
    char path[1024];
};

static SRWLOCK g_dir_lock = SRWLOCK_INIT;
static DIR *g_open_dirs[256];

static const char *dirfd_base_path(int dfd) {
    if (dfd >= 0x10000 && dfd < 0x10100) {
        DIR *d = g_open_dirs[dfd - 0x10000];
        if (d) return d->path;
    }
    return NULL;
}

DIR *opendir(const char *name) {
    if (!name || !*name) { errno = ENOENT; return NULL; }
    char pat[1024];
    snprintf(pat, sizeof pat, "%s\\*", name);
    DIR *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    strlcpy(d->path, name, sizeof d->path);
    d->slot = -1;
    d->h = FindFirstFileA(pat, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) {
        free(d);
        errno = ENOENT;
        return NULL;
    }
    d->first = 1;
    AcquireSRWLockExclusive(&g_dir_lock);
    for (int i = 0; i < 256; i++) {
        if (!g_open_dirs[i]) {
            g_open_dirs[i] = d;
            d->slot = i;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_dir_lock);
    return d;
}

int dirfd(DIR *d) {
    if (!d || d->slot < 0) return AT_FDCWD;
    return 0x10000 + d->slot;
}

struct dirent *readdir(DIR *d) {
    if (!d || d->h == INVALID_HANDLE_VALUE) return NULL;
    if (d->first) {
        d->first = 0;
    } else {
        if (!FindNextFileA(d->h, &d->fd)) return NULL;
    }
    strncpy(d->ent.d_name, d->fd.cFileName, sizeof(d->ent.d_name) - 1);
    d->ent.d_name[sizeof(d->ent.d_name) - 1] = 0;
    d->ent.d_type = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    d->ent.d_ino = 1;
    return &d->ent;
}

int closedir(DIR *d) {
    if (!d) return -1;
    if (d->slot >= 0 && d->slot < 256) {
        AcquireSRWLockExclusive(&g_dir_lock);
        if (g_open_dirs[d->slot] == d) g_open_dirs[d->slot] = NULL;
        ReleaseSRWLockExclusive(&g_dir_lock);
    }
    if (d->h != INVALID_HANDLE_VALUE) FindClose(d->h);
    free(d);
    return 0;
}

// ---------- dladdr / dlsym ----------
int dladdr(const void *addr, Dl_info *info) {
    if (!info || !addr) return 0;
    const uint8_t *base = (const uint8_t *)&__ImageBase;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + __ImageBase.e_lfanew);
    const uint8_t *p = (const uint8_t *)addr;
    if (p < base || p >= base + nt->OptionalHeader.SizeOfImage) return 0;
    memset(info, 0, sizeof *info);
    info->dli_fname = "dmhost.exe";
    info->dli_fbase = (void *)base;
    info->dli_sname = "dmhost";
    info->dli_saddr = (void *)base;
    return 1;
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle;
    HMODULE m = GetModuleHandleA("ucrtbase.dll");
    if (m) {
        void *p = (void *)GetProcAddress(m, symbol);
        if (p) return p;
    }
    return NULL;
}

// ---------- Mach VM stubs ----------
kern_return_t vm_read_overwrite(mach_port_t target, vm_address_t address, vm_size_t size, vm_address_t data, vm_size_t *outsize) {
    (void)target;
    SIZE_T got = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, (LPVOID)data, size, &got) && got == size) {
        if (outsize) *outsize = got;
        return KERN_SUCCESS;
    }
    return KERN_FAILURE;
}

kern_return_t vm_allocate(mach_port_t target, vm_address_t *address, vm_size_t size, int flags) {
    (void)target; (void)flags;
    void *p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) return KERN_FAILURE;
    if (address) *address = (vm_address_t)p;
    return KERN_SUCCESS;
}

kern_return_t vm_deallocate(mach_port_t target, vm_address_t address, vm_size_t size) {
    (void)target;
    munmap((void *)address, size);
    return KERN_SUCCESS;
}

kern_return_t vm_remap(mach_port_t target, vm_address_t *target_address, vm_size_t size, vm_address_t mask, int flags,
                       mach_port_t src_task, vm_address_t src_address, boolean_t copy, vm_prot_t *cur_protection,
                       vm_prot_t *max_protection, vm_inherit_t inheritance) {
    (void)target; (void)target_address; (void)size; (void)mask; (void)flags;
    (void)src_task; (void)src_address; (void)copy; (void)cur_protection; (void)max_protection; (void)inheritance;
    return KERN_FAILURE;
}

kern_return_t vm_region_64(mach_port_t target, vm_address_t *address, vm_size_t *size, int flavor, vm_region_info_t info, mach_msg_type_number_t *count, mach_port_t *object_name) {
    (void)target; (void)flavor; (void)count;
    if (object_name) *object_name = 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)(*address), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return KERN_FAILURE;
    *address = (vm_address_t)mbi.BaseAddress;
    *size = mbi.RegionSize;
    vm_region_basic_info_data_64_t *bi = (vm_region_basic_info_data_64_t *)info;
    if (bi) {
        memset(bi, 0, sizeof *bi);
        if (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY))
            bi->protection = VM_PROT_READ | VM_PROT_WRITE;
        else if (mbi.Protect & (PAGE_READONLY | PAGE_EXECUTE_READ))
            bi->protection = VM_PROT_READ;
    }
    return KERN_SUCCESS;
}

kern_return_t task_info(mach_port_t target, int flavor, task_info_t info, mach_msg_type_number_t *count) {
    (void)target; (void)count;
    if (flavor == TASK_VM_INFO && info) {
        task_vm_info_data_t *vi = (task_vm_info_data_t *)info;
        memset(vi, 0, sizeof *vi);
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
            vi->phys_footprint = pmc.WorkingSetSize;
        }
        return KERN_SUCCESS;
    }
    return KERN_FAILURE;
}

mach_port_t mach_thread_self(void) {
    return (mach_port_t)GetCurrentThreadId();
}

kern_return_t thread_info(mach_port_t target, int flavor, thread_info_t info, mach_msg_type_number_t *count) {
    (void)count;
    if (flavor == THREAD_BASIC_INFO && info && target) {
        thread_basic_info_data_t *bi = (thread_basic_info_data_t *)info;
        memset(bi, 0, sizeof *bi);
        HANDLE h = (target == GetCurrentThreadId()) ? GetCurrentThread() : OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)target);
        if (!h) return KERN_FAILURE;
        FILETIME c, e, k, u;
        BOOL ok = GetThreadTimes(h, &c, &e, &k, &u);
        if (h != GetCurrentThread()) CloseHandle(h);
        if (!ok) return KERN_FAILURE;
        uint64_t ku = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) / 10ull;
        uint64_t uu = (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime) / 10ull;
        bi->system_time.seconds = (uint32_t)(ku / 1000000ull);
        bi->system_time.microseconds = (uint32_t)(ku % 1000000ull);
        bi->user_time.seconds = (uint32_t)(uu / 1000000ull);
        bi->user_time.microseconds = (uint32_t)(uu % 1000000ull);
        bi->run_state = TH_STATE_RUNNING;
        return KERN_SUCCESS;
    }
    return KERN_FAILURE;
}

kern_return_t thread_suspend(thread_act_t target_act) {
    if (!target_act || target_act == GetCurrentThreadId()) return KERN_FAILURE;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, (DWORD)target_act);
    if (!h) return KERN_FAILURE;
    DWORD r = SuspendThread(h);
    CloseHandle(h);
    return r != (DWORD)-1 ? KERN_SUCCESS : KERN_FAILURE;
}

kern_return_t thread_resume(thread_act_t target_act) {
    if (!target_act || target_act == GetCurrentThreadId()) return KERN_FAILURE;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, (DWORD)target_act);
    if (!h) return KERN_FAILURE;
    DWORD r = ResumeThread(h);
    CloseHandle(h);
    return r != (DWORD)-1 ? KERN_SUCCESS : KERN_FAILURE;
}

kern_return_t thread_get_state(thread_act_t target_act, int flavor, thread_state_t old_state, mach_msg_type_number_t *old_stateCnt) {
    (void)old_stateCnt;
    if (flavor != ARM_THREAD_STATE64 || !old_state || !target_act || target_act == GetCurrentThreadId()) return KERN_FAILURE;
    HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, (DWORD)target_act);
    if (!h) return KERN_FAILURE;
    CONTEXT ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.ContextFlags = CONTEXT_FULL;
    BOOL ok = GetThreadContext(h, &ctx);
    CloseHandle(h);
    if (!ok) return KERN_FAILURE;
    arm_thread_state64_t *st = (arm_thread_state64_t *)old_state;
    memset(st, 0, sizeof *st);
    for (int i = 0; i < 29; i++) st->__x[i] = ctx.X[i];
    st->__fp = ctx.Fp;
    st->__lr = ctx.Lr;
    st->__sp = ctx.Sp;
    st->__pc = ctx.Pc;
    st->__cpsr = ctx.Cpsr;
    return KERN_SUCCESS;
}

kern_return_t thread_policy_set(mach_port_t thread, int flavor, thread_policy_t policy_info, mach_msg_type_number_t count) {
    (void)thread; (void)flavor; (void)policy_info; (void)count;
    return KERN_SUCCESS;
}

kern_return_t thread_policy_get(mach_port_t thread, int flavor, thread_policy_t policy_info, mach_msg_type_number_t *count, boolean_t *get_default) {
    (void)thread; (void)flavor; (void)policy_info; (void)count;
    if (get_default) *get_default = 1;
    return KERN_SUCCESS;
}

// ---------- Pthreads implementation on Win32 SRWLOCK / CONDITION_VARIABLE / CreateThread ----------
struct dm_win_thread {
    HANDLE h;
    DWORD tid;
    void *(*start)(void *);
    void *arg;
    void *ret;
    void *stack_base;
    size_t stack_size;
    int detached;
};

static __thread struct dm_win_thread *t_self;
static struct dm_win_thread g_main_thread;
static __thread void *t_tsd_slots[256];
static void (*g_key_destr[256])(void *);
static volatile LONG g_next_key = 1;

static void win_thread_setup_abi(struct dm_win_thread *t) {
    // Point tpidr_el0 at this thread's TSD slot table so rewritten guest code can read K18/KTLS/KTLR/KGATE at [tpidr_el0 + key*8]
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(t_tsd_slots) : "memory");
    ULONG_PTR lo = 0, hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);
    if (t) {
        t->stack_base = (void *)hi;
        if (!t->stack_size || hi > lo) t->stack_size = (size_t)(hi - lo);
    }
    // Pre-touch the Win32 stack pages down to lo + 64 KiB while the guard page is active, then widen NtTib.StackBase / StackLimit
    // so __chkstk and KiUserExceptionDispatcher (VEH) accept custom guest stacks (dm_thread_enter / dm_bigstack_enter).
    volatile uint8_t *sp_cur = (volatile uint8_t *)&lo;
    uintptr_t cur_pg = ((uintptr_t)sp_cur) & ~(uintptr_t)0xFFF;
    uintptr_t min_pg = (lo + 0x10000) & ~(uintptr_t)0xFFF;
    for (uintptr_t p = cur_pg; p > min_pg; p -= 0x1000) {
        volatile uint8_t touch = *(volatile uint8_t *)p;
        (void)touch;
    }
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    if (tib) {
        tib->StackLimit = (PVOID)0x1000;
        tib->StackBase = (PVOID)0x7FFFFFFFFFFFull;
    }
}

static void win_run_tsd_destructors(void) {
    LONG max_k = g_next_key;
    if (max_k >= 256) max_k = 255;
    for (int pass = 0; pass < 4; pass++) {
        int any = 0;
        for (LONG k = 1; k <= max_k; k++) {
            void *val = t_tsd_slots[k];
            void (*destr)(void *) = g_key_destr[k];
            if (val && destr) {
                t_tsd_slots[k] = NULL;
                destr(val);
                any = 1;
            }
        }
        if (!any) break;
    }
}

pthread_t pthread_self(void) {
    if (!t_self) {
        g_main_thread.h = GetCurrentThread();
        g_main_thread.tid = GetCurrentThreadId();
        g_main_thread.stack_size = 8u << 20;
        win_thread_setup_abi(&g_main_thread);
        t_self = &g_main_thread;
    } else {
        uint64_t base;
        __asm__ volatile("mrs %0, tpidr_el0" : "=r"(base));
        if (base != (uint64_t)(uintptr_t)t_tsd_slots)
            __asm__ volatile("msr tpidr_el0, %0" :: "r"(t_tsd_slots) : "memory");
    }
    return (pthread_t)t_self;
}

static unsigned __stdcall win_thread_entry(void *param) {
    struct dm_win_thread *t = (struct dm_win_thread *)param;
    t_self = t;
    win_thread_setup_abi(t);
    t->ret = t->start(t->arg);
    win_run_tsd_destructors();
    if (t->detached) {
        CloseHandle(t->h);
        free(t);
        t_self = NULL;
    }
    return 0;
}

void pthread_exit(void *retval) {
    win_run_tsd_destructors();
    struct dm_win_thread *t = t_self;
    if (t && t != &g_main_thread) {
        t->ret = retval;
        if (t->detached) {
            CloseHandle(t->h);
            free(t);
            t_self = NULL;
        }
    }
    _endthreadex(0);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    struct dm_win_thread *t = calloc(1, sizeof *t);
    if (!t) return ENOMEM;
    t->start = start_routine;
    t->arg = arg;
    t->stack_size = (attr && attr->stack_size) ? attr->stack_size : 0;
    t->detached = (attr && attr->detachstate == PTHREAD_CREATE_DETACHED) ? 1 : 0;
    unsigned tid = 0;
    uintptr_t h = _beginthreadex(NULL, (unsigned)t->stack_size, win_thread_entry, t, CREATE_SUSPENDED, &tid);
    if (!h) {
        free(t);
        return EAGAIN;
    }
    t->h = (HANDLE)h;
    t->tid = tid;
    if (thread) *thread = (pthread_t)t;
    ResumeThread(t->h);
    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    struct dm_win_thread *t = (struct dm_win_thread *)thread;
    if (!t) return EINVAL;
    WaitForSingleObject(t->h, INFINITE);
    if (retval) *retval = t->ret;
    CloseHandle(t->h);
    free(t);
    return 0;
}

int pthread_detach(pthread_t thread) {
    struct dm_win_thread *t = (struct dm_win_thread *)thread;
    if (!t) return EINVAL;
    t->detached = 1;
    return 0;
}

void *pthread_get_stackaddr_np(pthread_t t) {
    struct dm_win_thread *th = (struct dm_win_thread *)(t ? t : pthread_self());
    return th->stack_base;
}

size_t pthread_get_stacksize_np(pthread_t t) {
    struct dm_win_thread *th = (struct dm_win_thread *)(t ? t : pthread_self());
    return th->stack_size ? th->stack_size : (1u << 20);
}

int pthread_threadid_np(pthread_t t, uint64_t *thread_id) {
    struct dm_win_thread *th = (struct dm_win_thread *)(t ? t : pthread_self());
    if (thread_id) *thread_id = th ? th->tid : GetCurrentThreadId();
    return 0;
}

mach_port_t pthread_mach_thread_np(pthread_t t) {
    struct dm_win_thread *th = (struct dm_win_thread *)(t ? t : pthread_self());
    return th ? (mach_port_t)th->tid : (mach_port_t)GetCurrentThreadId();
}

pthread_t pthread_from_mach_thread_np(mach_port_t k) {
    (void)k;
    return NULL;
}

int pthread_kill(pthread_t t, int sig) {
    (void)t; (void)sig;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    (void)a;
    if (m) m->ptr = NULL;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
    AcquireSRWLockExclusive((PSRWLOCK)&m->ptr);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    return TryAcquireSRWLockExclusive((PSRWLOCK)&m->ptr) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    ReleaseSRWLockExclusive((PSRWLOCK)&m->ptr);
    return 0;
}

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) {
    (void)a;
    if (c) c->ptr = NULL;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *c) {
    (void)c;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    return SleepConditionVariableSRW((PCONDITION_VARIABLE)&c->ptr, (PSRWLOCK)&m->ptr, INFINITE, 0) ? 0 : ETIMEDOUT;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime) {
    DWORD ms = INFINITE;
    if (abstime) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        int64_t diff_ns = ((int64_t)abstime->tv_sec - (int64_t)now.tv_sec) * 1000000000ll + ((int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec);
        if (diff_ns <= 0) ms = 0;
        else ms = (DWORD)((diff_ns + 999999ll) / 1000000ll);
    }
    if (!SleepConditionVariableSRW((PCONDITION_VARIABLE)&c->ptr, (PSRWLOCK)&m->ptr, ms, 0)) {
        return ETIMEDOUT;
    }
    return 0;
}

int pthread_cond_timedwait_relative_np(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *reltime) {
    DWORD ms = INFINITE;
    if (reltime) {
        int64_t ns = (int64_t)reltime->tv_sec * 1000000000ll + (int64_t)reltime->tv_nsec;
        if (ns <= 0) ms = 0;
        else ms = (DWORD)((ns + 999999ll) / 1000000ll);
    }
    if (!SleepConditionVariableSRW((PCONDITION_VARIABLE)&c->ptr, (PSRWLOCK)&m->ptr, ms, 0)) {
        return ETIMEDOUT;
    }
    return 0;
}

int pthread_cond_signal(pthread_cond_t *c) {
    WakeConditionVariable((PCONDITION_VARIABLE)&c->ptr);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c) {
    WakeAllConditionVariable((PCONDITION_VARIABLE)&c->ptr);
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *rw, const void *a) {
    (void)a;
    if (rw) { rw->ptr = NULL; rw->excl = 0; }
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
    (void)rw;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
    AcquireSRWLockShared((PSRWLOCK)&rw->ptr);
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
    AcquireSRWLockExclusive((PSRWLOCK)&rw->ptr);
    rw->excl = 1;
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
    if (rw->excl) {
        rw->excl = 0;
        ReleaseSRWLockExclusive((PSRWLOCK)&rw->ptr);
    } else {
        ReleaseSRWLockShared((PSRWLOCK)&rw->ptr);
    }
    return 0;
}

static BOOL CALLBACK win_once_cb(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)ctx;
    void (*fn)(void) = (void (*)(void))param;
    fn();
    return TRUE;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    InitOnceExecuteOnce((PINIT_ONCE)&once_control->ptr, win_once_cb, (PVOID)init_routine, NULL);
    return 0;
}

// Thread-specific data: maintains a per-thread 256-slot table pointed to by tpidr_el0 for K18/KTLS/KTLR/KGATE
int pthread_key_create(pthread_key_t *key, void (*destr_function)(void *)) {
    LONG k = InterlockedIncrement(&g_next_key);
    if (k >= 256) return EAGAIN;
    g_key_destr[k] = destr_function;
    if (key) *key = (pthread_key_t)k;
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *pointer) {
    if (key >= 256) return EINVAL;
    if (!t_self) pthread_self();
    else {
        uint64_t base;
        __asm__ volatile("mrs %0, tpidr_el0" : "=r"(base));
        if (base != (uint64_t)(uintptr_t)t_tsd_slots)
            __asm__ volatile("msr tpidr_el0, %0" :: "r"(t_tsd_slots) : "memory");
    }
    t_tsd_slots[key] = (void *)pointer;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= 256) return NULL;
    return t_tsd_slots[key];
}

int pthread_attr_init(pthread_attr_t *attr) {
    if (attr) memset(attr, 0, sizeof *attr);
    return 0;
}
int pthread_attr_destroy(pthread_attr_t *attr) { (void)attr; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
    if (attr) attr->stack_size = stacksize;
    return 0;
}
int pthread_attr_set_qos_class_np(pthread_attr_t *attr, qos_class_t qos, int rel) {
    if (attr) { attr->qos = (int)qos; attr->rel_prio = rel; }
    return 0;
}
int pthread_set_qos_class_self_np(qos_class_t qos, int rel) { (void)qos; (void)rel; return 0; }
int pthread_get_qos_class_np(pthread_t t, qos_class_t *qos, int *rel) {
    (void)t;
    if (qos) *qos = QOS_CLASS_DEFAULT;
    if (rel) *rel = 0;
    return 0;
}
int pthread_setschedparam(pthread_t t, int policy, const struct sched_param *param) {
    (void)t; (void)policy; (void)param;
    return 0;
}
int pthread_getschedparam(pthread_t t, int *policy, struct sched_param *param) {
    (void)t;
    if (policy) *policy = SCHED_OTHER;
    if (param) param->sched_priority = 31;
    return 0;
}
int pthread_setname_np(const char *name) { (void)name; return 0; }

// ---------- Signals and Vectored Exception Handling ----------
static struct sigaction g_sigactions[32];
static PVOID g_veh_handle = NULL;

static LONG WINAPI win_veh_handler(PEXCEPTION_POINTERS ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    int sig = 0;
    uint64_t fault_addr = 0;
    int is_write = 0;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        sig = SIGSEGV;
        is_write = (ep->ExceptionRecord->NumberParameters >= 1 && ep->ExceptionRecord->ExceptionInformation[0] == 1);
        fault_addr = (ep->ExceptionRecord->NumberParameters >= 2) ? (uint64_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
        break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        sig = SIGBUS;
        fault_addr = (uint64_t)ep->ExceptionRecord->ExceptionAddress;
        break;
    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_SINGLE_STEP:
        sig = SIGTRAP;
        fault_addr = (uint64_t)ep->ExceptionRecord->ExceptionAddress;
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        sig = SIGILL;
        fault_addr = (uint64_t)ep->ExceptionRecord->ExceptionAddress;
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (sig <= 0 || sig >= 32) return EXCEPTION_CONTINUE_SEARCH;
    void (*sa_fn)(int, siginfo_t *, void *) = g_sigactions[sig].sa_sigaction;
    if (!sa_fn) return EXCEPTION_CONTINUE_SEARCH;

    siginfo_t si;
    memset(&si, 0, sizeof si);
    si.si_signo = sig;
    si.si_addr = (void *)(uintptr_t)fault_addr;

    ucontext_t uc;
    memset(&uc, 0, sizeof uc);
    uc.uc_mcontext = &uc.__mcontext_data;
    CONTEXT *ctx = ep->ContextRecord;
    for (int i = 0; i < 29; i++) uc.__mcontext_data.__ss.__x[i] = ctx->X[i];
    uc.__mcontext_data.__ss.__fp = ctx->Fp;
    uc.__mcontext_data.__ss.__lr = ctx->Lr;
    uc.__mcontext_data.__ss.__sp = ctx->Sp;
    uc.__mcontext_data.__ss.__pc = ctx->Pc;
    uc.__mcontext_data.__ss.__cpsr = ctx->Cpsr;
    uc.__mcontext_data.__es.__far = fault_addr;
    uc.__mcontext_data.__es.__esr = is_write ? (1u << 6) : 0u;
    memcpy(uc.__mcontext_data.__ns.__v, ctx->V, sizeof uc.__mcontext_data.__ns.__v);
    uc.__mcontext_data.__ns.__fpcr = ctx->Fpcr;
    uc.__mcontext_data.__ns.__fpsr = ctx->Fpsr;

    sa_fn(sig, &si, &uc);

    // If sa_sigaction returned (did not call _exit), a fault hook (nvn_dirty.c / watch_fault) or trap hook (probe.c) handled it
    for (int i = 0; i < 29; i++) ctx->X[i] = uc.__mcontext_data.__ss.__x[i];
    ctx->Fp = uc.__mcontext_data.__ss.__fp;
    ctx->Lr = uc.__mcontext_data.__ss.__lr;
    ctx->Sp = uc.__mcontext_data.__ss.__sp;
    ctx->Pc = uc.__mcontext_data.__ss.__pc;
    ctx->Cpsr = uc.__mcontext_data.__ss.__cpsr;
    memcpy(ctx->V, uc.__mcontext_data.__ns.__v, sizeof uc.__mcontext_data.__ns.__v);
    ctx->Fpcr = uc.__mcontext_data.__ns.__fpcr;
    ctx->Fpsr = uc.__mcontext_data.__ns.__fpsr;
    return EXCEPTION_CONTINUE_EXECUTION;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    if (sig <= 0 || sig >= 32) return -1;
    if (!g_veh_handle) {
        g_veh_handle = AddVectoredExceptionHandler(1, win_veh_handler);
    }
    if (oact) *oact = g_sigactions[sig];
    if (act) g_sigactions[sig] = *act;
    return 0;
}

static volatile LONG g_alarm_seq = 0;
struct win_alarm_arg { LONG seq; unsigned int seconds; };
static unsigned __stdcall win_alarm_thread(void *p) {
    struct win_alarm_arg *a = (struct win_alarm_arg *)p;
    LONG seq = a->seq;
    unsigned int sec = a->seconds;
    free(a);
    Sleep(sec * 1000u);
    if (g_alarm_seq == seq) {
        void (*sa_fn)(int, siginfo_t *, void *) = g_sigactions[SIGALRM].sa_sigaction;
        if (sa_fn) {
            siginfo_t si;
            ucontext_t uc;
            memset(&si, 0, sizeof si);
            memset(&uc, 0, sizeof uc);
            si.si_signo = SIGALRM;
            uc.uc_mcontext = &uc.__mcontext_data;
            sa_fn(SIGALRM, &si, &uc);
        }
        _exit(128 + SIGALRM);
    }
    return 0;
}

unsigned int alarm(unsigned int seconds) {
    LONG seq = InterlockedIncrement(&g_alarm_seq);
    if (seconds > 0) {
        struct win_alarm_arg *a = malloc(sizeof *a);
        if (a) {
            a->seq = seq;
            a->seconds = seconds;
            uintptr_t h = _beginthreadex(NULL, 65536, win_alarm_thread, a, 0, NULL);
            if (h) CloseHandle((HANDLE)h);
            else free(a);
        }
    }
    return 0;
}

// ---------- Dispatch semaphore ----------
dispatch_semaphore_t dispatch_semaphore_create(long value) {
    return (dispatch_semaphore_t)CreateSemaphoreW(NULL, value, 0x7fffffff, NULL);
}

long dispatch_semaphore_signal(dispatch_semaphore_t dsema) {
    LONG prev = 0;
    ReleaseSemaphore((HANDLE)dsema, 1, &prev);
    return prev == 0;
}

long dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout) {
    DWORD ms = (timeout == DISPATCH_TIME_FOREVER) ? INFINITE : (DWORD)((timeout + 999999ull) / 1000000ull);
    return WaitForSingleObject((HANDLE)dsema, ms) == WAIT_OBJECT_0 ? 0 : -1;
}

void dispatch_release(void *object) {
    if (object) CloseHandle((HANDLE)object);
}

dispatch_time_t dispatch_time(dispatch_time_t when, int64_t delta) {
    (void)when;
    return delta > 0 ? (dispatch_time_t)delta : 0;
}

// ---------- CoreFoundation CFDictionary (used by hle_nvn.c for shader keys) ----------
struct __CFDictionary {
    SRWLOCK lock;
    size_t count, cap;
    const void **keys;
    const void **vals;
};

CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef allocator, long capacity, const void *keyCallBacks, const void *valueCallBacks) {
    (void)allocator; (void)capacity; (void)keyCallBacks; (void)valueCallBacks;
    struct __CFDictionary *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cap = 256;
    d->keys = calloc(d->cap, sizeof(void *));
    d->vals = calloc(d->cap, sizeof(void *));
    return d;
}

const void *CFDictionaryGetValue(CFDictionaryRef theDict, const void *key) {
    if (!theDict) return NULL;
    struct __CFDictionary *d = (struct __CFDictionary *)theDict;
    AcquireSRWLockShared(&d->lock);
    const void *res = NULL;
    for (size_t i = 0; i < d->count; i++) {
        if (d->keys[i] == key) { res = d->vals[i]; break; }
    }
    ReleaseSRWLockShared(&d->lock);
    return res;
}

void CFDictionarySetValue(CFMutableDictionaryRef theDict, const void *key, const void *value) {
    if (!theDict) return;
    AcquireSRWLockExclusive(&theDict->lock);
    for (size_t i = 0; i < theDict->count; i++) {
        if (theDict->keys[i] == key) {
            theDict->vals[i] = value;
            ReleaseSRWLockExclusive(&theDict->lock);
            return;
        }
    }
    if (theDict->count == theDict->cap) {
        size_t ncap = theDict->cap * 2;
        theDict->keys = realloc(theDict->keys, ncap * sizeof(void *));
        theDict->vals = realloc(theDict->vals, ncap * sizeof(void *));
        theDict->cap = ncap;
    }
    theDict->keys[theDict->count] = key;
    theDict->vals[theDict->count] = value;
    theDict->count++;
    ReleaseSRWLockExclusive(&theDict->lock);
}

void CFDictionaryRemoveValue(CFMutableDictionaryRef theDict, const void *key) {
    if (!theDict) return;
    AcquireSRWLockExclusive(&theDict->lock);
    for (size_t i = 0; i < theDict->count; i++) {
        if (theDict->keys[i] == key) {
            theDict->keys[i] = theDict->keys[theDict->count - 1];
            theDict->vals[i] = theDict->vals[theDict->count - 1];
            theDict->count--;
            break;
        }
    }
    ReleaseSRWLockExclusive(&theDict->lock);
}

// ---------- Apple malloc zone compatibility (used by hle_uemalloc.c) ----------
static malloc_zone_t g_default_zone;
malloc_zone_t *malloc_create_zone(size_t start_size, unsigned flags) {
    (void)start_size; (void)flags;
    return &g_default_zone;
}
void malloc_set_zone_name(malloc_zone_t *zone, const char *name) { (void)zone; (void)name; }
void *malloc_zone_malloc(malloc_zone_t *zone, size_t size) { (void)zone; return malloc(size); }
void *malloc_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size) {
    (void)zone; (void)alignment;
    return malloc(size);
}
void *malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size) { (void)zone; return realloc(ptr, size); }
void malloc_zone_free(malloc_zone_t *zone, void *ptr) { (void)zone; free(ptr); }
size_t malloc_size(const void *ptr) { return ptr ? _msize((void *)ptr) : 0; }
size_t malloc_good_size(size_t size) { return (size + 15) & ~(size_t)15; }

// ---------- SHA-256 (CommonCrypto/CommonDigest.h) ----------
static const uint32_t k_sha256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + k_sha256[i] + w[i];
        uint32_t S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

int CC_SHA256_Init(CC_SHA256_CTX *c) {
    c->state[0] = 0x6a09e667u; c->state[1] = 0xbb67ae85u;
    c->state[2] = 0x3c6ef372u; c->state[3] = 0xa54ff53au;
    c->state[4] = 0x510e527fu; c->state[5] = 0x9b05688cu;
    c->state[6] = 0x1f83d9abu; c->state[7] = 0x5be0cd19u;
    c->bitcount = 0;
    return 1;
}

int CC_SHA256_Update(CC_SHA256_CTX *c, const void *data, CC_LONG len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t idx = (size_t)((c->bitcount >> 3) & 63);
    c->bitcount += (uint64_t)len << 3;
    if (idx) {
        size_t left = 64 - idx;
        if (len < left) {
            memcpy(c->buffer + idx, p, len);
            return 1;
        }
        memcpy(c->buffer + idx, p, left);
        sha256_block(c->state, c->buffer);
        p += left;
        len -= (CC_LONG)left;
    }
    while (len >= 64) {
        sha256_block(c->state, p);
        p += 64;
        len -= 64;
    }
    if (len) memcpy(c->buffer, p, len);
    return 1;
}

int CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *c) {
    size_t idx = (size_t)((c->bitcount >> 3) & 63);
    c->buffer[idx++] = 0x80;
    if (idx > 56) {
        memset(c->buffer + idx, 0, 64 - idx);
        sha256_block(c->state, c->buffer);
        idx = 0;
    }
    memset(c->buffer + idx, 0, 56 - idx);
    for (int i = 0; i < 8; i++) c->buffer[56 + i] = (uint8_t)(c->bitcount >> (56 - i * 8));
    sha256_block(c->state, c->buffer);
    for (int i = 0; i < 8; i++) {
        md[i * 4]     = (uint8_t)(c->state[i] >> 24);
        md[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
        md[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
        md[i * 4 + 3] = (uint8_t)(c->state[i]);
    }
    return 1;
}

unsigned char *CC_SHA256(const void *data, CC_LONG len, unsigned char *md) {
    CC_SHA256_CTX c;
    CC_SHA256_Init(&c);
    CC_SHA256_Update(&c, data, len);
    CC_SHA256_Final(md, &c);
    return md;
}
