// Windows ARM64 POSIX / Mach / Apple framework compatibility declarations for dmhost.
// Avoids including <windows.h> here so Win32 macros never pollute dm/host or dm/gfx translation units.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE 1
#endif
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 1
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <process.h>
#include <direct.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Basic POSIX types ----------
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t useconds_t;
typedef int pid_t;
#ifndef _MODE_T_
#define _MODE_T_
typedef unsigned short mode_t;
#endif

// Use 64-bit file offsets and stat structures (MSVC defaults off_t and struct stat.st_size to 32-bit long, which overflows on >2 GB RomFS images)
#define off_t int64_t
#define stat _stat64
#define fstat _fstat64
#define lseek _lseeki64

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

// ---------- File / fcntl / stat ----------
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#ifndef O_DSYNC
#define O_DSYNC 0
#endif
#ifndef O_SYNC
#define O_SYNC 0
#endif
#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#ifndef F_GETPATH
#define F_GETPATH 50
#endif
#ifndef F_FULLFSYNC
#define F_FULLFSYNC 51
#endif
#ifndef F_RDADVISE
#define F_RDADVISE 44
#endif
struct radvisory { off_t ra_offset; int ra_count; };

#ifndef S_IFMT
#define S_IFMT _S_IFMT
#endif
#ifndef S_IFDIR
#define S_IFDIR _S_IFDIR
#endif
#ifndef S_IFREG
#define S_IFREG _S_IFREG
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) 0
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) 0
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & S_IFMT) == _S_IFCHR)
#endif
#ifndef S_ISBLK
#define S_ISBLK(m) 0
#endif

#ifndef R_OK
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#endif

ssize_t pread(int fd, void *buf, size_t count, int64_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, int64_t offset);
int ftruncate(int fd, int64_t length);
int truncate(const char *path, int64_t length);
int fsync(int fd);
int fcntl(int fd, int cmd, ...);
int lstat(const char *path, struct stat *st);
int fstatat(int dirfd, const char *path, struct stat *st, int flags);
int unlinkat(int dirfd, const char *path, int flags);
int mkdirat(int dirfd, const char *path, mode_t mode);
int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int symlink(const char *target, const char *linkpath);

int dm_win_mkdir(const char *path, int mode);
#define mkdir(p, m) dm_win_mkdir(p, m)

// ---------- String / stdlib / stdio extensions ----------
size_t strlcpy(char *dst, const char *src, size_t siz);
size_t strlcat(char *dst, const char *src, size_t siz);
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#ifndef strtok_r
#define strtok_r strtok_s
#endif

int posix_memalign(void **memptr, size_t alignment, size_t size);
void dm_win_free(void *p);
#define free(p) dm_win_free(p)

int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
uint32_t arc4random(void);
void arc4random_buf(void *buf, size_t nbytes);

static inline void flockfile(FILE *f) { _lock_file(f); }
static inline int ftrylockfile(FILE *f) { (void)f; return 0; }
static inline void funlockfile(FILE *f) { _unlock_file(f); }

// ---------- Time / sleep ----------
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_UPTIME_RAW 8
#define CLOCK_THREAD_CPUTIME_ID 3
#endif

#if !defined(_TIMEVAL_DEFINED) && !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#define _TIMEVAL_DEFINED
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int clock_gettime(int clk_id, struct timespec *tp);
uint64_t clock_gettime_nsec_np(int clk_id);
int gettimeofday(struct timeval *tv, void *tz);
int nanosleep(const struct timespec *req, struct timespec *rem);
int usleep(useconds_t usec);
unsigned int sleep(unsigned int seconds);
int mach_wait_until(uint64_t deadline);
void dm_win_tzset(void);
#define tzset dm_win_tzset
extern char *tzname[2];

struct dm_win_tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    int64_t tm_gmtoff;
    const char *tm_zone;
};
#define tm dm_win_tm
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
time_t timegm(struct tm *tm);
struct tm *dm_win_localtime(const time_t *timep);
time_t dm_win_mktime(struct tm *tm_p);
size_t dm_win_strftime(char *s, size_t max, const char *format, const struct tm *tm_p);
#define localtime dm_win_localtime
#define mktime dm_win_mktime
#define strftime dm_win_strftime

// ---------- Process / system info / resource / clonefile / xattr ----------
int getpagesize(void);
static inline int getdtablesize(void) { return 2048; }
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 58
#define _SC_PAGESIZE 29
#define _SC_PAGE_SIZE 29
#endif
long sysconf(int name);
int sched_yield(void);

struct sched_param {
    int sched_priority;
};
#define SCHED_OTHER 1
#define SCHED_FIFO  4
#define SCHED_RR    2
static inline int sched_get_priority_min(int policy) { (void)policy; return 15; }
static inline int sched_get_priority_max(int policy) { (void)policy; return 47; }

#define PRIO_PROCESS 0
#define PRIO_DARWIN_THREAD 3
#define PRIO_DARWIN_BG 0x1000
static inline int setpriority(int which, int who, int prio) { (void)which; (void)who; (void)prio; return 0; }
static inline int getpriority(int which, int who) { (void)which; (void)who; return 0; }

#define RUSAGE_SELF 0
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
};
int getrusage(int who, struct rusage *usage);

typedef uint64_t rlim_t;
struct rlimit { rlim_t rlim_cur; rlim_t rlim_max; };
#define RLIMIT_CORE   4
#define RLIMIT_NOFILE 8
static inline int getrlimit(int resource, struct rlimit *rlp) { (void)resource; if (rlp) { rlp->rlim_cur = 4096; rlp->rlim_max = 8192; } return 0; }
static inline int setrlimit(int resource, const struct rlimit *rlp) { (void)resource; (void)rlp; return 0; }

#define CTL_KERN 1
#define KERN_PROC 14
#define KERN_PROC_PID 1
struct kinfo_proc {
    struct {
        struct timeval p_starttime;
    } kp_proc;
};
int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

struct statfs {
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
};
int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);

#define RENAME_SECLUDE 0x00000001
#define RENAME_SWAP    0x00000002
#define RENAME_EXCL    0x00000004
int clonefile(const char *src, const char *dst, int flags);
int renamex_np(const char *from, const char *to, unsigned int flags);

#ifndef ENOATTR
#define ENOATTR 93
#endif
#define XATTR_NOFOLLOW 0x0001
ssize_t getxattr(const char *path, const char *name, void *value, size_t size, uint32_t position, int options);
int setxattr(const char *path, const char *name, const void *value, size_t size, uint32_t position, int options);
int removexattr(const char *path, const char *name, int options);

// ---------- Signals / ucontext ----------
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGTRAP
#define SIGTRAP 5
#endif
#ifndef SIGBUS
#define SIGBUS 10
#endif
#ifndef SIGUSR1
#define SIGUSR1 30
#endif
#ifndef SIGUSR2
#define SIGUSR2 31
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGSTOP
#define SIGSTOP 17
#endif

#ifndef SA_SIGINFO
#define SA_SIGINFO 0x0040
#define SA_ONSTACK 0x0001
#define SA_RESTART 0x0002
#define SA_RESETHAND 0x0004
#define SA_NODEFER 0x0010
#endif
#ifndef SIG_BLOCK
#define SIG_BLOCK 1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3
#endif
#ifndef SS_DISABLE
#define SS_DISABLE 0x0004
#endif

typedef uint32_t sigset_t;
typedef struct {
    void *si_addr;
    int si_signo;
    int si_code;
} siginfo_t;

typedef struct {
    void *ss_sp;
    size_t ss_size;
    int ss_flags;
} stack_t;

struct __darwin_arm_exception_state64 {
    uint64_t __far;
    uint32_t __esr;
    uint32_t __exception;
};
struct __darwin_arm_thread_state64 {
    uint64_t __x[29];
    uint64_t __fp;
    uint64_t __lr;
    uint64_t __sp;
    uint64_t __pc;
    uint32_t __cpsr;
    uint32_t __pad;
};
struct __darwin_arm_neon_state64 {
    __uint128_t __v[32];
    uint32_t __fpsr;
    uint32_t __fpcr;
};
#define _STRUCT_ARM_THREAD_STATE64 struct __darwin_arm_thread_state64
#define _STRUCT_ARM_NEON_STATE64 struct __darwin_arm_neon_state64
typedef struct __darwin_arm_thread_state64 arm_thread_state64_t;
typedef int *thread_state_t;
#define ARM_THREAD_STATE64 6
#define ARM_THREAD_STATE64_COUNT ((mach_msg_type_number_t)(sizeof(arm_thread_state64_t) / sizeof(uint32_t)))
#define __darwin_arm_thread_state64_get_pc(ts) ((ts).__pc)
#define __darwin_arm_thread_state64_get_lr(ts) ((ts).__lr)
#define __darwin_arm_thread_state64_get_fp(ts) ((ts).__fp)
#define __darwin_arm_thread_state64_get_sp(ts) ((ts).__sp)
struct __darwin_mcontext64 {
    struct __darwin_arm_exception_state64 __es;
    struct __darwin_arm_thread_state64 __ss;
    struct __darwin_arm_neon_state64 __ns;
};
typedef struct ucontext_t {
    struct __darwin_mcontext64 __mcontext_data;
    struct __darwin_mcontext64 *uc_mcontext;
    sigset_t uc_sigmask;
} ucontext_t;

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
};

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int sigaltstack(const stack_t *ss, stack_t *oss);
int sigemptyset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset);
unsigned int alarm(unsigned int seconds);
static inline int pause(void) { sleep(1); return 0; }

// ---------- Memory mapping (sys/mman.h) ----------
#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_SHARED  0x0001
#define MAP_PRIVATE 0x0002
#define MAP_FIXED   0x0010
#define MAP_ANON    0x1000
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FAILED  ((void *)-1)

#define MS_SYNC       0x0010
#define MS_ASYNC      0x0001
#define MS_INVALIDATE 0x0002

#define MADV_DONTNEED 4
#define MADV_FREE     5
#define MADV_WILLNEED 3

void *mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t offset);
int munmap(void *addr, size_t len);
int mprotect(void *addr, size_t len, int prot);
int madvise(void *addr, size_t len, int advice);
int msync(void *addr, size_t len, int flags);
int mincore(const void *addr, size_t len, char *vec);

// ---------- Pthreads (pthread.h) ----------
typedef void *pthread_t;
typedef unsigned long pthread_key_t;
typedef struct { void *ptr; } pthread_mutex_t;
typedef struct { void *ptr; } pthread_cond_t;
typedef struct { void *ptr; int excl; } pthread_rwlock_t;
typedef struct { void *ptr; } pthread_once_t;
typedef struct { size_t stack_size; int detachstate; int qos; int rel_prio; } pthread_attr_t;
typedef struct { int type; } pthread_mutexattr_t;
typedef struct { int dummy; } pthread_condattr_t;
typedef struct { int dummy; } pthread_rwlockattr_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 0 }
#define PTHREAD_RWLOCK_INITIALIZER { 0, 0 }
#define PTHREAD_ONCE_INIT { 0 }
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 2

typedef enum {
    QOS_CLASS_USER_INTERACTIVE = 0x21,
    QOS_CLASS_USER_INITIATED = 0x19,
    QOS_CLASS_DEFAULT = 0x15,
    QOS_CLASS_UTILITY = 0x11,
    QOS_CLASS_BACKGROUND = 0x09,
    QOS_CLASS_UNSPECIFIED = 0x00,
} qos_class_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
static inline int pthread_equal(pthread_t t1, pthread_t t2) { return t1 == t2; }
void pthread_exit(void *retval);
int pthread_kill(pthread_t thread, int sig);
int pthread_setname_np(const char *name);
int pthread_threadid_np(pthread_t thread, uint64_t *thread_id);
int pthread_set_qos_class_self_np(qos_class_t qos_class, int relative_priority);
int pthread_get_qos_class_np(pthread_t t, qos_class_t *qos, int *rel);
int pthread_attr_set_qos_class_np(pthread_attr_t *attr, qos_class_t qos, int rel);
typedef void *pthread_override_t;
static inline pthread_override_t pthread_override_qos_class_start_np(pthread_t t, qos_class_t qos, int rel) { (void)t; (void)qos; (void)rel; return NULL; }
static inline int pthread_override_qos_class_end_np(pthread_override_t ov) { (void)ov; return 0; }
int pthread_setschedparam(pthread_t t, int policy, const struct sched_param *param);
int pthread_getschedparam(pthread_t t, int *policy, struct sched_param *param);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
void *pthread_get_stackaddr_np(pthread_t t);
size_t pthread_get_stacksize_np(pthread_t t);

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);
int pthread_cond_timedwait_relative_np(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *reltime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const void *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

// ---------- dlfcn.h ----------
typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_DEFAULT ((void *)-2)

int dladdr(const void *addr, Dl_info *info);
void *dlopen(const char *path, int mode);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
char *dlerror(void);

// ---------- dirent.h ----------
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
#define DT_LNK     10

struct dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[260];
};
typedef struct DIR DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
int dirfd(DIR *dirp);

// ---------- Mach / Darwin shims ----------
typedef int kern_return_t;
typedef uint32_t mach_port_t;
#ifndef MACH_PORT_NULL
#define MACH_PORT_NULL 0
#endif
typedef uintptr_t vm_address_t;
typedef size_t vm_size_t;
#ifndef vm_page_size
#define vm_page_size ((vm_size_t)4096)
#endif
typedef uintptr_t mach_vm_address_t;
typedef uint64_t mach_vm_size_t;
typedef int vm_prot_t;
typedef int vm_inherit_t;
typedef int boolean_t;
typedef unsigned int natural_t;
typedef unsigned int mach_msg_type_number_t;
typedef void *thread_info_t;
typedef void *vm_region_info_t;
typedef void *vm_region_recurse_info_t;
typedef void *task_info_t;
typedef void *thread_policy_t;
typedef mach_port_t thread_act_t;
typedef mach_port_t *thread_act_array_t;

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define VM_PROT_NONE    0x00
#define VM_PROT_READ    0x01
#define VM_PROT_WRITE   0x02
#define VM_PROT_EXECUTE 0x04
#define VM_FLAGS_ANYWHERE 0x0001
#define VM_FLAGS_FIXED    0x0000
#define VM_FLAGS_OVERWRITE 0x4000
#define VM_INHERIT_NONE 2

typedef struct {
    uint32_t numer;
    uint32_t denom;
} mach_timebase_info_data_t;
typedef mach_timebase_info_data_t *mach_timebase_info_t;

uint64_t dm_win_qpc_ns(void);
uint64_t mach_absolute_time(void);
static inline kern_return_t mach_timebase_info(mach_timebase_info_t info) {
    if (info) { info->numer = 1; info->denom = 1; }
    return KERN_SUCCESS;
}
static inline mach_port_t mach_task_self(void) { return 1; }
mach_port_t mach_thread_self(void);
static inline kern_return_t mach_port_deallocate(mach_port_t task, mach_port_t name) { (void)task; (void)name; return KERN_SUCCESS; }
mach_port_t pthread_mach_thread_np(pthread_t t);
pthread_t pthread_from_mach_thread_np(mach_port_t k);

kern_return_t vm_read_overwrite(mach_port_t target_task, vm_address_t address, vm_size_t size, vm_address_t data, vm_size_t *outsize);
kern_return_t vm_allocate(mach_port_t target, vm_address_t *address, vm_size_t size, int flags);
kern_return_t vm_deallocate(mach_port_t target, vm_address_t address, vm_size_t size);
kern_return_t vm_remap(mach_port_t target, vm_address_t *target_address, vm_size_t size, vm_address_t mask, int flags,
                       mach_port_t src_task, vm_address_t src_address, boolean_t copy, vm_prot_t *cur_protection,
                       vm_prot_t *max_protection, vm_inherit_t inheritance);

typedef struct {
    int protection;
    int max_protection;
    unsigned int inheritance;
    boolean_t shared;
    boolean_t reserved;
    uint64_t offset;
    int behavior;
    unsigned short user_wired_count;
} vm_region_basic_info_data_64_t;
#define VM_REGION_BASIC_INFO_64 9
#define VM_REGION_BASIC_INFO_COUNT_64 ((mach_msg_type_number_t)(sizeof(vm_region_basic_info_data_64_t) / sizeof(int)))

kern_return_t vm_region_64(mach_port_t target_task, vm_address_t *address, vm_size_t *size, int flavor, vm_region_info_t info, mach_msg_type_number_t *infoCnt, mach_port_t *object_name);

#define TASK_VM_INFO 22
typedef struct {
    uint64_t virtual_size;
    uint64_t resident_size;
    uint64_t phys_footprint;
    uint64_t internal;
    uint64_t compressed;
    int64_t ledger_tag_graphics_footprint;
    int64_t ledger_tag_graphics_footprint_compressed;
    int64_t limit_bytes_remaining;
} task_vm_info_data_t;
#define TASK_VM_INFO_COUNT ((mach_msg_type_number_t)(sizeof(task_vm_info_data_t) / sizeof(uint32_t)))

#define TASK_POWER_INFO_V2 23
typedef struct {
    struct {
        uint64_t total_user;
        uint64_t total_system;
        uint64_t task_interrupt_wakeups;
        uint64_t task_platform_idle_wakeups;
    } cpu_energy;
    struct {
        uint64_t task_gpu_utilisation;
    } gpu_energy;
    uint64_t task_energy;
    uint64_t task_ptime;
} task_power_info_v2_data_t;
#define TASK_POWER_INFO_V2_COUNT ((mach_msg_type_number_t)(sizeof(task_power_info_v2_data_t) / sizeof(uint32_t)))

kern_return_t task_info(mach_port_t target, int flavor, task_info_t info, mach_msg_type_number_t *count);

typedef struct {
    uint64_t pth_user_time;
    uint64_t pth_system_time;
    int32_t pth_cpu_usage;
    int32_t pth_policy;
    int32_t pth_run_state;
    int32_t pth_flags;
    int32_t pth_sleep_time;
    int32_t pth_curpri;
    int32_t pth_priority;
    int32_t pth_maxpriority;
    char pth_name[64];
} thread_extended_info_data_t;
#define THREAD_EXTENDED_INFO 5
#define THREAD_EXTENDED_INFO_COUNT ((mach_msg_type_number_t)(sizeof(thread_extended_info_data_t) / sizeof(int)))

typedef struct {
    struct { uint32_t seconds, microseconds; } user_time;
    struct { uint32_t seconds, microseconds; } system_time;
    int32_t cpu_usage;
    int32_t policy;
    int32_t run_state;
    int32_t flags;
    int32_t suspend_count;
    int32_t sleep_time;
} thread_basic_info_data_t;
#define THREAD_BASIC_INFO 3
#define THREAD_BASIC_INFO_COUNT ((mach_msg_type_number_t)(sizeof(thread_basic_info_data_t) / sizeof(int)))
#define TH_STATE_RUNNING 1

typedef struct {
    uint64_t thread_id;
    uint64_t thread_handle;
    uint64_t dispatch_qaddr;
} thread_identifier_info_data_t;
#define THREAD_IDENTIFIER_INFO 4
#define THREAD_IDENTIFIER_INFO_COUNT ((mach_msg_type_number_t)(sizeof(thread_identifier_info_data_t) / sizeof(int)))

typedef struct { int affinity_tag; } thread_affinity_policy_data_t;
#define THREAD_AFFINITY_POLICY 4
#define THREAD_AFFINITY_POLICY_COUNT 1

#define THREAD_TIME_CONSTRAINT_POLICY 2
typedef struct {
    uint32_t period;
    uint32_t computation;
    uint32_t constraint;
    boolean_t preemptible;
} thread_time_constraint_policy_data_t;
#define THREAD_TIME_CONSTRAINT_POLICY_COUNT 4

kern_return_t thread_info(mach_port_t target_act, int flavor, thread_info_t thread_info_out, mach_msg_type_number_t *thread_info_outCnt);
kern_return_t thread_policy_set(mach_port_t thread, int flavor, thread_policy_t policy_info, mach_msg_type_number_t count);
kern_return_t thread_policy_get(mach_port_t thread, int flavor, thread_policy_t policy_info, mach_msg_type_number_t *count, boolean_t *get_default);

static inline kern_return_t mach_vm_region(mach_port_t task, mach_vm_address_t *address, mach_vm_size_t *size, int flavor, vm_region_info_t info, mach_msg_type_number_t *infoCnt, mach_port_t *object_name) {
    vm_address_t a = (vm_address_t)*address; vm_size_t s = 0;
    kern_return_t kr = vm_region_64(task, &a, &s, flavor, info, infoCnt, object_name);
    *address = a; *size = s; return kr;
}
static inline kern_return_t mach_vm_remap(mach_port_t target_task, mach_vm_address_t *target_address, mach_vm_size_t size, mach_vm_address_t mask, int flags, mach_port_t src_task, mach_vm_address_t src_address, boolean_t copy, vm_prot_t *cur_protection, vm_prot_t *max_protection, vm_inherit_t inheritance) {
    (void)target_task; (void)target_address; (void)size; (void)mask; (void)flags; (void)src_task; (void)src_address; (void)copy; (void)cur_protection; (void)max_protection; (void)inheritance;
    return KERN_FAILURE;
}
static inline kern_return_t task_threads(mach_port_t target_task, thread_act_array_t *act_list, mach_msg_type_number_t *act_listCnt) {
    (void)target_task; *act_list = NULL; *act_listCnt = 0; return KERN_FAILURE;
}
kern_return_t thread_suspend(thread_act_t target_act);
kern_return_t thread_resume(thread_act_t target_act);
kern_return_t thread_get_state(thread_act_t target_act, int flavor, thread_state_t old_state, mach_msg_type_number_t *old_stateCnt);

// ---------- Mach-O section lookup (mapped to PE/COFF sections on Windows) ----------
extern int _mh_execute_header;
const uint8_t *getsectiondata(const void *mhp, const char *segname, const char *sectname, unsigned long *size);

// ---------- Instruction cache ----------
void sys_icache_invalidate(void *start, size_t len);

#ifdef __cplusplus
}
#endif
