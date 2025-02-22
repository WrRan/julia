// This file is a part of Julia. License is MIT: https://julialang.org/license

/*
  sys.c
  I/O and operating system utility functions
*/
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

#include "julia.h"
#include "julia_internal.h"

#ifdef _OS_WINDOWS_
#include <psapi.h>
#else
#include <unistd.h>
#if !defined(_SC_NPROCESSORS_ONLN) || defined(_OS_FREEBSD_) || defined(_OS_DARWIN_)
// try secondary location for _SC_NPROCESSORS_ONLN, or for HW_AVAILCPU on BSDs
#include <sys/sysctl.h>
#endif
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <grp.h>
#endif

#ifndef _OS_WINDOWS_
// for getrusage
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <sys/types.h> // for jl_raise_debugger
#elif !defined(_OS_WINDOWS_)
#include <link.h>
#endif

#ifdef __SSE__
#include <xmmintrin.h>
#endif

#ifdef _COMPILER_MSAN_ENABLED_
#include <sanitizer/msan_interface.h>
#endif

#include "julia_assert.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_OS_WINDOWS_) && !defined(_COMPILER_GCC_)
JL_DLLEXPORT char *dirname(char *);
#else
#include <libgen.h>
#endif

JL_DLLEXPORT int jl_sizeof_off_t(void) { return sizeof(off_t); }
#ifndef _OS_WINDOWS_
JL_DLLEXPORT int jl_sizeof_mode_t(void) { return sizeof(mode_t); }
JL_DLLEXPORT int jl_ftruncate(int fd, int64_t length)
{
    return ftruncate(fd, (off_t)length);
}
JL_DLLEXPORT int64_t jl_lseek(int fd, int64_t offset, int whence)
{
    return lseek(fd, (off_t)offset, whence);
}
JL_DLLEXPORT ssize_t jl_pwrite(int fd, const void *buf, size_t count, int64_t offset)
{
    return pwrite(fd, buf, count, (off_t)offset);
}
JL_DLLEXPORT void *jl_mmap(void *addr, size_t length, int prot, int flags,
                           int fd, int64_t offset)
{
    return mmap(addr, length, prot, flags, fd, (off_t)offset);
}
#else
JL_DLLEXPORT int64_t jl_lseek(HANDLE fd, int64_t offset, int whence)
{
    LARGE_INTEGER tell;
    tell.QuadPart = offset;
    if (SetFilePointerEx(fd, tell, &tell, whence) == 0)
        return -1;
    return tell.QuadPart;
}
#endif
JL_DLLEXPORT int jl_sizeof_ios_t(void) { return sizeof(ios_t); }

JL_DLLEXPORT long jl_ios_fd(ios_t *s) { return s->fd; }

JL_DLLEXPORT int32_t jl_nb_available(ios_t *s)
{
    return (int32_t)(s->size - s->bpos);
}

// --- dir/file stuff ---

JL_DLLEXPORT int jl_sizeof_uv_fs_t(void) { return sizeof(uv_fs_t); }
JL_DLLEXPORT char *jl_uv_fs_t_ptr(uv_fs_t *req) { return (char*)req->ptr; }
JL_DLLEXPORT char *jl_uv_fs_t_path(uv_fs_t *req) { return (char*)req->path; }

// --- stat ---
JL_DLLEXPORT int jl_sizeof_stat(void) { return sizeof(uv_stat_t); }

JL_DLLEXPORT int32_t jl_stat(const char *path, char *statbuf) JL_NOTSAFEPOINT
{
    uv_fs_t req;
    int ret;

    // Ideally one would use the statbuf for the storage in req, but
    // it's not clear that this is possible using libuv
    ret = uv_fs_stat(unused_uv_loop_arg, &req, path, NULL);
    if (ret == 0)
        memcpy(statbuf, req.ptr, sizeof(uv_stat_t));
    uv_fs_req_cleanup(&req);
    return ret;
}

JL_DLLEXPORT int32_t jl_lstat(const char *path, char *statbuf)
{
    uv_fs_t req;
    int ret;

    ret = uv_fs_lstat(unused_uv_loop_arg, &req, path, NULL);
    if (ret == 0)
        memcpy(statbuf, req.ptr, sizeof(uv_stat_t));
    uv_fs_req_cleanup(&req);
    return ret;
}

JL_DLLEXPORT int32_t jl_fstat(uv_os_fd_t fd, char *statbuf)
{
    uv_fs_t req;
    int ret;

    ret = uv_fs_fstat(unused_uv_loop_arg, &req, fd, NULL);
    if (ret == 0)
        memcpy(statbuf, req.ptr, sizeof(uv_stat_t));
    uv_fs_req_cleanup(&req);
    return ret;
}

JL_DLLEXPORT unsigned int jl_stat_dev(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_dev;
}

JL_DLLEXPORT unsigned int jl_stat_ino(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_ino;
}

JL_DLLEXPORT unsigned int jl_stat_mode(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_mode;
}

JL_DLLEXPORT unsigned int jl_stat_nlink(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_nlink;
}

JL_DLLEXPORT unsigned int jl_stat_uid(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_uid;
}

JL_DLLEXPORT unsigned int jl_stat_gid(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_gid;
}

JL_DLLEXPORT unsigned int jl_stat_rdev(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_rdev;
}

JL_DLLEXPORT uint64_t jl_stat_size(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_size;
}

JL_DLLEXPORT uint64_t jl_stat_blksize(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_blksize;
}

JL_DLLEXPORT uint64_t jl_stat_blocks(char *statbuf)
{
    return ((uv_stat_t*)statbuf)->st_blocks;
}

/*
// atime is stupid, let's not support it
JL_DLLEXPORT double jl_stat_atime(char *statbuf)
{
  uv_stat_t *s;
  s = (uv_stat_t*)statbuf;
  return (double)s->st_atim.tv_sec + (double)s->st_atim.tv_nsec * 1e-9;
}
*/

JL_DLLEXPORT double jl_stat_mtime(char *statbuf)
{
    uv_stat_t *s;
    s = (uv_stat_t*)statbuf;
    return (double)s->st_mtim.tv_sec + (double)s->st_mtim.tv_nsec * 1e-9;
}

JL_DLLEXPORT double jl_stat_ctime(char *statbuf)
{
    uv_stat_t *s;
    s = (uv_stat_t*)statbuf;
    return (double)s->st_ctim.tv_sec + (double)s->st_ctim.tv_nsec * 1e-9;
}

JL_DLLEXPORT unsigned long jl_getuid(void)
{
#ifdef _OS_WINDOWS_
    return -1;
#else
    return getuid();
#endif
}

JL_DLLEXPORT unsigned long jl_geteuid(void)
{
#ifdef _OS_WINDOWS_
    return -1;
#else
    return geteuid();
#endif
}

JL_DLLEXPORT int jl_os_get_passwd(uv_passwd_t *pwd, unsigned long uid)
{
#ifdef _OS_WINDOWS_
  return UV_ENOTSUP;
#else
  // taken directly from libuv
  struct passwd pw;
  struct passwd* result;
  char* buf;
  size_t bufsize;
  size_t name_size;
  size_t homedir_size;
  size_t shell_size;
  size_t gecos_size;
  long initsize;
  int r;

  if (pwd == NULL)
    return UV_EINVAL;

  initsize = sysconf(_SC_GETPW_R_SIZE_MAX);

  if (initsize <= 0)
    bufsize = 4096;
  else
    bufsize = (size_t) initsize;

  buf = NULL;

  for (;;) {
    free(buf);
    buf = (char*)malloc(bufsize);

    if (buf == NULL)
      return UV_ENOMEM;

    r = getpwuid_r(uid, &pw, buf, bufsize, &result);

    if (r != ERANGE)
      break;

    bufsize *= 2;
  }

  if (r != 0) {
    free(buf);
    return -r;
  }

  if (result == NULL) {
    free(buf);
    return UV_ENOENT;
  }

  /* Allocate memory for the username, gecos, shell, and home directory. */
  name_size = strlen(pw.pw_name) + 1;
  homedir_size = strlen(pw.pw_dir) + 1;
  shell_size = strlen(pw.pw_shell) + 1;

#ifdef __MVS__
  gecos_size = 0; /* pw_gecos does not exist on zOS. */
#else
  if (pw.pw_gecos != NULL)
    gecos_size = strlen(pw.pw_gecos) + 1;
  else
    gecos_size = 0;
#endif

  pwd->username = (char*)malloc(name_size +
                         homedir_size +
                         shell_size +
                         gecos_size);

  if (pwd->username == NULL) {
    free(buf);
    return UV_ENOMEM;
  }

  /* Copy the username */
  memcpy(pwd->username, pw.pw_name, name_size);

  /* Copy the home directory */
  pwd->homedir = pwd->username + name_size;
  memcpy(pwd->homedir, pw.pw_dir, homedir_size);

  /* Copy the shell */
  pwd->shell = pwd->homedir + homedir_size;
  memcpy(pwd->shell, pw.pw_shell, shell_size);

  /* Copy the gecos field */
#ifdef __MVS__
  pwd->gecos = NULL;  /* pw_gecos does not exist on zOS. */
#else
  if (pw.pw_gecos == NULL) {
    pwd->gecos = NULL;
  } else {
    pwd->gecos = pwd->shell + shell_size;
    memcpy(pwd->gecos, pw.pw_gecos, gecos_size);
  }
#endif

  /* Copy the uid and gid */
  pwd->uid = pw.pw_uid;
  pwd->gid = pw.pw_gid;

  free(buf);

  return 0;
#endif
}

typedef struct jl_group_s {
    char* groupname;
    unsigned long gid;
    char** members;
} jl_group_t;

JL_DLLEXPORT int jl_os_get_group(jl_group_t *grp, unsigned long gid)
{
#ifdef _OS_WINDOWS_
  return UV_ENOTSUP;
#else
  // modified directly from uv_os_get_password
  struct group gp;
  struct group* result;
  char* buf;
  char* gr_mem;
  size_t bufsize;
  size_t name_size;
  long members;
  size_t mem_size;
  long initsize;
  int r;

  if (grp == NULL)
    return UV_EINVAL;

  initsize = sysconf(_SC_GETGR_R_SIZE_MAX);

  if (initsize <= 0)
    bufsize = 4096;
  else
    bufsize = (size_t) initsize;

  buf = NULL;

  for (;;) {
    free(buf);
    buf = (char*)malloc(bufsize);

    if (buf == NULL)
      return UV_ENOMEM;

    r = getgrgid_r(gid, &gp, buf, bufsize, &result);

    if (r != ERANGE)
      break;

    bufsize *= 2;
  }

  if (r != 0) {
    free(buf);
    return -r;
  }

  if (result == NULL) {
    free(buf);
    return UV_ENOENT;
  }

  /* Allocate memory for the groupname and members. */
  name_size = strlen(gp.gr_name) + 1;
  members = 0;
  mem_size = sizeof(char*);
  for (r = 0; gp.gr_mem[r] != NULL; r++) {
    mem_size += strlen(gp.gr_mem[r]) + 1 + sizeof(char*);
    members++;
  }

  gr_mem = (char*)malloc(name_size + mem_size);
  if (gr_mem == NULL) {
    free(buf);
    return UV_ENOMEM;
  }

  /* Copy the members */
  grp->members = (char**) gr_mem;
  grp->members[members] = NULL;
  gr_mem = (char*) ((char**) gr_mem + members + 1);
  for (r = 0; r < members; r++) {
    grp->members[r] = gr_mem;
    gr_mem = stpcpy(gr_mem, gp.gr_mem[r]) + 1;
  }
  assert(gr_mem == (char*)grp->members + mem_size);

  /* Copy the groupname */
  grp->groupname = gr_mem;
  memcpy(grp->groupname, gp.gr_name, name_size);
  gr_mem += name_size;

  /* Copy the gid */
  grp->gid = gp.gr_gid;

  free(buf);

  return 0;
#endif
}

JL_DLLEXPORT void jl_os_free_group(jl_group_t *grp)
{
  if (grp == NULL)
    return;

  /*
    The memory for is allocated in a single uv__malloc() call. The base of the
    pointer is stored in grp->members, so that is the only field that needs
    to be freed.
  */
  free(grp->members);
  grp->members = NULL;
  grp->groupname = NULL;
}

// --- buffer manipulation ---

JL_DLLEXPORT jl_array_t *jl_take_buffer(ios_t *s)
{
    size_t n;
    jl_array_t *a;
    if (s->buf == &s->local[0]) {
        // small data case. copies, but this can be avoided using the
        // technique of jl_readuntil below.
        a = jl_pchar_to_array(s->buf, s->size);
        ios_trunc(s, 0);
    }
    else {
        char *b = ios_take_buffer(s, &n);
        a = jl_ptr_to_array_1d(jl_array_uint8_type, b, n-1, 1);
    }
    return a;
}

// str: if 1 return a string, otherwise return a Vector{UInt8}
// chomp:
//   0 - keep delimiter
//   1 - remove 1 byte delimiter
//   2 - remove 2 bytes \r\n if present
JL_DLLEXPORT jl_value_t *jl_readuntil(ios_t *s, uint8_t delim, uint8_t str, uint8_t chomp)
{
    jl_array_t *a;
    // manually inlined common case
    char *pd = (char*)memchr(s->buf + s->bpos, delim, (size_t)(s->size - s->bpos));
    if (pd) {
        size_t n = pd - (s->buf + s->bpos) + 1;
        size_t nchomp = 0;
        if (chomp) {
            nchomp = chomp == 2 ? ios_nchomp(s, n) : 1;
        }
        if (str) {
            jl_value_t *str = jl_pchar_to_string(s->buf + s->bpos, n - nchomp);
            s->bpos += n;
            return str;
        }
        a = jl_alloc_array_1d(jl_array_uint8_type, n - nchomp);
        memcpy(jl_array_data(a), s->buf + s->bpos, n - nchomp);
        s->bpos += n;
    }
    else {
        a = jl_alloc_array_1d(jl_array_uint8_type, 80);
        ios_t dest;
        ios_mem(&dest, 0);
        ios_setbuf(&dest, (char*)a->data, 80, 0);
        size_t n = ios_copyuntil(&dest, s, delim);
        if (chomp && n > 0 && dest.buf[n - 1] == delim) {
            n--;
            if (chomp == 2 && n > 0 && dest.buf[n - 1] == '\r') {
                n--;
            }
            int truncret = ios_trunc(&dest, n); // it should always be possible to truncate dest
            assert(truncret == 0);
            (void)truncret; // ensure the variable is used to avoid warnings
        }
        if (dest.buf != a->data) {
            a = jl_take_buffer(&dest);
        }
        else {
            a->length = n;
            a->nrows = n;
            ((char*)a->data)[n] = '\0';
        }
        if (str) {
            JL_GC_PUSH1(&a);
            jl_value_t *st = jl_array_to_string(a);
            JL_GC_POP();
            return st;
        }
    }
    return (jl_value_t*)a;
}

JL_DLLEXPORT int jl_ios_buffer_n(ios_t *s, const size_t n)
{
    size_t space, ret;
    do {
        space = (size_t)(s->size - s->bpos);
        ret = ios_readprep(s, n);
        if (space == ret && ret < n)
            return 1;
    } while (ret < n);
    return 0;
}

JL_DLLEXPORT uint64_t jl_ios_get_nbyte_int(ios_t *s, const size_t n)
{
    assert(n <= 8);
    uint64_t x = 0;
    uint8_t *buf = (uint8_t*)&s->buf[s->bpos];
    if (n == 8) {
        // expecting loop unrolling optimization
        for (size_t i = 0; i < 8; i++)
            x |= (uint64_t)buf[i] << (i << 3);
    }
    else if (n >= 4) {
        // expecting loop unrolling optimization
        for (size_t i = 0; i < 4; i++)
            x |= (uint64_t)buf[i] << (i << 3);
        for (size_t i = 4; i < n; i++)
            x |= (uint64_t)buf[i] << (i << 3);
    }
    else {
        for (size_t i = 0; i < n; i++)
            x |= (uint64_t)buf[i] << (i << 3);
    }
    s->bpos += n;
    return x;
}

// -- syscall utilities --

JL_DLLEXPORT int jl_errno(void) JL_NOTSAFEPOINT { return errno; }
JL_DLLEXPORT void jl_set_errno(int e) JL_NOTSAFEPOINT { errno = e; }

// -- get the number of CPU threads (logical cores) --

#ifdef _OS_WINDOWS_
typedef DWORD (WINAPI *GAPC)(WORD);
#ifndef ALL_PROCESSOR_GROUPS
#define ALL_PROCESSOR_GROUPS 0xffff
#endif
#endif

// Apple's M1 processor is a big.LITTLE style processor, with 4x "performance"
// cores, and 4x "efficiency" cores.  Because Julia expects to be able to run
// things like heavy linear algebra workloads on all cores, it's best for us
// to only spawn as many threads as there are performance cores.  Once macOS
// 12 is released, we'll be able to query the multiple "perf levels" of the
// cores of a CPU (see this PR [0] to pytorch/cpuinfo for an example) but
// until it's released, we will just recognize the M1 by its CPU family
// identifier, then subtract how many efficiency cores we know it has.

JL_DLLEXPORT int jl_cpu_threads(void) JL_NOTSAFEPOINT
{
#if defined(HW_AVAILCPU) && defined(HW_NCPU)
    size_t len = 4;
    int32_t count;
    int nm[2] = {CTL_HW, HW_AVAILCPU};
    sysctl(nm, 2, &count, &len, NULL, 0);
    if (count < 1) {
        nm[1] = HW_NCPU;
        sysctl(nm, 2, &count, &len, NULL, 0);
        if (count < 1) { count = 1; }
    }

#if defined(__APPLE__) && defined(_CPU_AARCH64_)
    // Manually subtract efficiency cores for Apple's big.LITTLE cores
    int32_t family = 0;
    len = 4;
    sysctlbyname("hw.cpufamily", &family, &len, NULL, 0);
    if (family >= 1 && count > 1) {
        if (family == CPUFAMILY_ARM_FIRESTORM_ICESTORM) {
            // We know the Apple M1 has 4 efficiency cores, so subtract them out.
            count -= 4;
        }
    }
#endif
    return count;
#elif defined(_SC_NPROCESSORS_ONLN)
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count < 1)
        return 1;
    return count;
#elif defined(_OS_WINDOWS_)
    //Try to get WIN7 API method
    GAPC gapc;
    if (jl_dlsym(jl_kernel32_handle, "GetActiveProcessorCount", (void **)&gapc, 0)) {
        return gapc(ALL_PROCESSOR_GROUPS);
    }
    else { //fall back on GetSystemInfo
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return info.dwNumberOfProcessors;
    }
#else
#warning "cpu core detection not defined for this platform"
    return 1;
#endif
}


// -- high resolution timers --
// Returns time in nanosec
JL_DLLEXPORT uint64_t jl_hrtime(void) JL_NOTSAFEPOINT
{
    return uv_hrtime();
}

// -- iterating the environment --

#ifdef __APPLE__
#include <crt_externs.h>
#else
#if !defined(_OS_WINDOWS_) || defined(_COMPILER_GCC_)
extern char **environ;
#endif
#endif

JL_DLLEXPORT jl_value_t *jl_environ(int i)
{
#ifdef __APPLE__
    char **environ = *_NSGetEnviron();
#endif
    char *env = environ[i];
    return env ? jl_pchar_to_string(env, strlen(env)) : jl_nothing;
}

// -- child process status --

#if defined _OS_WINDOWS_
/* Native Woe32 API.  */
#include <process.h>
#define waitpid(pid,statusp,options) _cwait (statusp, pid, WAIT_CHILD)
#define WAIT_T int
#define WTERMSIG(x) ((x) & 0xff) /* or: SIGABRT ?? */
#define WCOREDUMP(x) 0
#define WEXITSTATUS(x) (((x) >> 8) & 0xff) /* or: (x) ?? */
#define WIFSIGNALED(x) (WTERMSIG (x) != 0) /* or: ((x) == 3) ?? */
#define WIFEXITED(x) (WTERMSIG (x) == 0) /* or: ((x) != 3) ?? */
#define WIFSTOPPED(x) 0
#define WSTOPSIG(x) 0 //Is this correct?
#endif

int jl_process_exited(int status)      { return WIFEXITED(status); }
int jl_process_signaled(int status)    { return WIFSIGNALED(status); }
int jl_process_stopped(int status)     { return WIFSTOPPED(status); }

int jl_process_exit_status(int status) { return WEXITSTATUS(status); }
int jl_process_term_signal(int status) { return WTERMSIG(status); }
int jl_process_stop_signal(int status) { return WSTOPSIG(status); }

// -- access to std filehandles --

JL_STREAM *JL_STDIN  = (JL_STREAM*)STDIN_FILENO;
JL_STREAM *JL_STDOUT = (JL_STREAM*)STDOUT_FILENO;
JL_STREAM *JL_STDERR = (JL_STREAM*)STDERR_FILENO;

JL_DLLEXPORT JL_STREAM *jl_stdin_stream(void)  { return JL_STDIN; }
JL_DLLEXPORT JL_STREAM *jl_stdout_stream(void) { return JL_STDOUT; }
JL_DLLEXPORT JL_STREAM *jl_stderr_stream(void) { return JL_STDERR; }

// -- processor native alignment information --

JL_DLLEXPORT void jl_native_alignment(uint_t *int8align, uint_t *int16align, uint_t *int32align,
                                      uint_t *int64align, uint_t *float32align, uint_t *float64align)
{
    *int8align = __alignof(uint8_t);
    *int16align = __alignof(uint16_t);
    *int32align = __alignof(uint32_t);
    *int64align = __alignof(uint64_t);
    *float32align = __alignof(float);
    *float64align = __alignof(double);
}

JL_DLLEXPORT jl_value_t *jl_is_char_signed(void)
{
    return ((char)255) < 0 ? jl_true : jl_false;
}

// -- misc sysconf info --

#ifdef _OS_WINDOWS_
static long cachedPagesize = 0;
JL_DLLEXPORT long jl_getpagesize(void)
{
    if (!cachedPagesize) {
        SYSTEM_INFO systemInfo;
        GetSystemInfo (&systemInfo);
        cachedPagesize = systemInfo.dwPageSize;
    }
    return cachedPagesize;
}
#else
JL_DLLEXPORT long jl_getpagesize(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size != -1);
    return page_size;
}
#endif

#ifdef _OS_WINDOWS_
static long cachedAllocationGranularity = 0;
JL_DLLEXPORT long jl_getallocationgranularity(void) JL_NOTSAFEPOINT
{
    if (!cachedAllocationGranularity) {
        SYSTEM_INFO systemInfo;
        GetSystemInfo (&systemInfo);
        cachedAllocationGranularity = systemInfo.dwAllocationGranularity;
    }
    return cachedAllocationGranularity;
}
#else
JL_DLLEXPORT long jl_getallocationgranularity(void) JL_NOTSAFEPOINT
{
    return jl_getpagesize();
}
#endif

JL_DLLEXPORT long jl_SC_CLK_TCK(void)
{
#ifndef _OS_WINDOWS_
    return sysconf(_SC_CLK_TCK);
#else
    return 0;
#endif
}

// Takes a handle (as returned from dlopen()) and returns the absolute path to the image loaded
JL_DLLEXPORT const char *jl_pathname_for_handle(void *handle)
{
    if (!handle)
        return NULL;

#ifdef __APPLE__
    // Iterate through all images currently in memory
    for (int32_t i = _dyld_image_count() - 1; i >= 0 ; i--) {
        // dlopen() each image, check handle
        const char *image_name = _dyld_get_image_name(i);
        void *probe_lib = jl_load_dynamic_library(image_name, JL_RTLD_DEFAULT | JL_RTLD_NOLOAD, 0);
        jl_dlclose(probe_lib);

        // If the handle is the same as what was passed in (modulo mode bits), return this image name
        if (((intptr_t)handle & (-4)) == ((intptr_t)probe_lib & (-4)))
            return image_name;
    }

#elif defined(_OS_WINDOWS_)

    wchar_t *pth16 = (wchar_t*)malloc_s(32768 * sizeof(*pth16)); // max long path length
    DWORD n16 = GetModuleFileNameW((HMODULE)handle, pth16, 32768);
    if (n16 <= 0) {
        free(pth16);
        return NULL;
    }
    pth16[n16] = L'\0';
    DWORD n8 = WideCharToMultiByte(CP_UTF8, 0, pth16, -1, NULL, 0, NULL, NULL);
    if (n8 == 0) {
        free(pth16);
        return NULL;
    }
    char *filepath = (char*)malloc_s(++n8);
    if (!WideCharToMultiByte(CP_UTF8, 0, pth16, -1, filepath, n8, NULL, NULL)) {
        free(pth16);
        free(filepath);
        return NULL;
    }
    free(pth16);
    return filepath;

#else // Linux, FreeBSD, ...

    struct link_map *map;
    dlinfo(handle, RTLD_DI_LINKMAP, &map);
#ifdef _COMPILER_MSAN_ENABLED_
    __msan_unpoison(&map,sizeof(struct link_map*));
    if (map) {
        __msan_unpoison(map, sizeof(struct link_map));
        __msan_unpoison_string(map->l_name);
    }
#endif
    if (map)
        return map->l_name;

#endif
    return NULL;
}

#ifdef _OS_WINDOWS_
// Get a list of all the modules in this process.
JL_DLLEXPORT int jl_dllist(jl_array_t *list)
{
    DWORD cb, cbNeeded;
    HMODULE *hMods = NULL;
    unsigned int i;
    cbNeeded = 1024 * sizeof(*hMods);
    do {
        cb = cbNeeded;
        hMods = (HMODULE*)realloc_s(hMods, cb);
        if (!EnumProcessModulesEx(GetCurrentProcess(), hMods, cb, &cbNeeded, LIST_MODULES_ALL)) {
          free(hMods);
          return FALSE;
        }
    } while (cb < cbNeeded);
    for (i = 0; i < cbNeeded / sizeof(HMODULE); i++) {
        const char *path = jl_pathname_for_handle(hMods[i]);
        if (path == NULL)
            continue;
        jl_array_grow_end((jl_array_t*)list, 1);
        jl_value_t *v = jl_cstr_to_string(path);
        free((char*)path);
        jl_array_ptr_set(list, jl_array_dim0(list) - 1, v);
    }
    free(hMods);
    return TRUE;
}
#endif

JL_DLLEXPORT void jl_raise_debugger(void)
{
#if defined(_OS_WINDOWS_)
    if (IsDebuggerPresent() == 1)
        DebugBreak();
#else
    raise(SIGTRAP);
#endif // _OS_WINDOWS_
}

JL_DLLEXPORT jl_sym_t *jl_get_UNAME(void) JL_NOTSAFEPOINT
{
    return jl_symbol(JL_BUILD_UNAME);
}

JL_DLLEXPORT jl_sym_t *jl_get_ARCH(void) JL_NOTSAFEPOINT
{
    return jl_symbol(JL_BUILD_ARCH);
}

JL_DLLEXPORT size_t jl_maxrss(void)
{
#if defined(_OS_WINDOWS_)
    PROCESS_MEMORY_COUNTERS counter;
    GetProcessMemoryInfo( GetCurrentProcess( ), &counter, sizeof(counter) );
    return (size_t)counter.PeakWorkingSetSize;

// FIXME: `rusage` is available on OpenBSD, DragonFlyBSD and NetBSD as well.
//        All of them return `ru_maxrss` in kilobytes.
#elif defined(_OS_LINUX_) || defined(_OS_DARWIN_) || defined (_OS_FREEBSD_)
    struct rusage rusage;
    getrusage( RUSAGE_SELF, &rusage );

#if defined(_OS_LINUX_) || defined(_OS_FREEBSD_)
    return (size_t)(rusage.ru_maxrss * 1024);
#else
    return (size_t)rusage.ru_maxrss;
#endif

#else
    return (size_t)0;
#endif
}

#ifdef __cplusplus
}
#endif
