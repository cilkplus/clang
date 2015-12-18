//===-- sanitizer_posix_libcdep.cc ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements libc-dependent POSIX-specific functions
// from sanitizer_libc.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_POSIX
#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_platform_limits_posix.h"
#include "sanitizer_posix.h"
#include "sanitizer_procmaps.h"
#include "sanitizer_stacktrace.h"
#include "sanitizer_symbolizer.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#if SANITIZER_FREEBSD
// The MAP_NORESERVE define has been removed in FreeBSD 11.x, and even before
// that, it was never implemented.  So just define it to zero.
#undef  MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

namespace __sanitizer {

u32 GetUid() {
  return getuid();
}

uptr GetThreadSelf() {
  return (uptr)pthread_self();
}

void FlushUnneededShadowMemory(uptr addr, uptr size) {
  madvise((void*)addr, size, MADV_DONTNEED);
}

void NoHugePagesInRegion(uptr addr, uptr size) {
#ifdef MADV_NOHUGEPAGE  // May not be defined on old systems.
  madvise((void *)addr, size, MADV_NOHUGEPAGE);
#endif  // MADV_NOHUGEPAGE
}

void DontDumpShadowMemory(uptr addr, uptr length) {
#ifdef MADV_DONTDUMP
  madvise((void *)addr, length, MADV_DONTDUMP);
#endif
}

static rlim_t getlim(int res) {
  rlimit rlim;
  CHECK_EQ(0, getrlimit(res, &rlim));
  return rlim.rlim_cur;
}

static void setlim(int res, rlim_t lim) {
  // The following magic is to prevent clang from replacing it with memset.
  volatile struct rlimit rlim;
  rlim.rlim_cur = lim;
  rlim.rlim_max = lim;
  if (setrlimit(res, const_cast<struct rlimit *>(&rlim))) {
    Report("ERROR: %s setrlimit() failed %d\n", SanitizerToolName, errno);
    Die();
  }
}

void DisableCoreDumperIfNecessary() {
  if (common_flags()->disable_coredump) {
    setlim(RLIMIT_CORE, 0);
  }
}

bool StackSizeIsUnlimited() {
  rlim_t stack_size = getlim(RLIMIT_STACK);
  return (stack_size == RLIM_INFINITY);
}

void SetStackSizeLimitInBytes(uptr limit) {
  setlim(RLIMIT_STACK, (rlim_t)limit);
  CHECK(!StackSizeIsUnlimited());
}

bool AddressSpaceIsUnlimited() {
  rlim_t as_size = getlim(RLIMIT_AS);
  return (as_size == RLIM_INFINITY);
}

void SetAddressSpaceUnlimited() {
  setlim(RLIMIT_AS, RLIM_INFINITY);
  CHECK(AddressSpaceIsUnlimited());
}

void SleepForSeconds(int seconds) {
  sleep(seconds);
}

void SleepForMillis(int millis) {
  usleep(millis * 1000);
}

void Abort() {
  abort();
}

int Atexit(void (*function)(void)) {
#ifndef SANITIZER_GO
  return atexit(function);
#else
  return 0;
#endif
}

bool SupportsColoredOutput(fd_t fd) {
  return isatty(fd) != 0;
}

#ifndef SANITIZER_GO
// TODO(glider): different tools may require different altstack size.
static const uptr kAltStackSize = SIGSTKSZ * 4;  // SIGSTKSZ is not enough.

void SetAlternateSignalStack() {
  stack_t altstack, oldstack;
  CHECK_EQ(0, sigaltstack(0, &oldstack));
  // If the alternate stack is already in place, do nothing.
  // Android always sets an alternate stack, but it's too small for us.
  if (!SANITIZER_ANDROID && !(oldstack.ss_flags & SS_DISABLE)) return;
  // TODO(glider): the mapped stack should have the MAP_STACK flag in the
  // future. It is not required by man 2 sigaltstack now (they're using
  // malloc()).
  void* base = MmapOrDie(kAltStackSize, __func__);
  altstack.ss_sp = (char*) base;
  altstack.ss_flags = 0;
  altstack.ss_size = kAltStackSize;
  CHECK_EQ(0, sigaltstack(&altstack, 0));
}

void UnsetAlternateSignalStack() {
  stack_t altstack, oldstack;
  altstack.ss_sp = 0;
  altstack.ss_flags = SS_DISABLE;
  altstack.ss_size = kAltStackSize;  // Some sane value required on Darwin.
  CHECK_EQ(0, sigaltstack(&altstack, &oldstack));
  UnmapOrDie(oldstack.ss_sp, oldstack.ss_size);
}

typedef void (*sa_sigaction_t)(int, siginfo_t *, void *);
static void MaybeInstallSigaction(int signum,
                                  SignalHandlerType handler) {
  if (!IsDeadlySignal(signum))
    return;
  struct sigaction sigact;
  internal_memset(&sigact, 0, sizeof(sigact));
  sigact.sa_sigaction = (sa_sigaction_t)handler;
  // Do not block the signal from being received in that signal's handler.
  // Clients are responsible for handling this correctly.
  sigact.sa_flags = SA_SIGINFO | SA_NODEFER;
  if (common_flags()->use_sigaltstack) sigact.sa_flags |= SA_ONSTACK;
  CHECK_EQ(0, internal_sigaction(signum, &sigact, 0));
  VReport(1, "Installed the sigaction for signal %d\n", signum);
}

void InstallDeadlySignalHandlers(SignalHandlerType handler) {
  // Set the alternate signal stack for the main thread.
  // This will cause SetAlternateSignalStack to be called twice, but the stack
  // will be actually set only once.
  if (common_flags()->use_sigaltstack) SetAlternateSignalStack();
  MaybeInstallSigaction(SIGSEGV, handler);
  MaybeInstallSigaction(SIGBUS, handler);
  MaybeInstallSigaction(SIGABRT, handler);
}
#endif  // SANITIZER_GO

bool IsAccessibleMemoryRange(uptr beg, uptr size) {
  uptr page_size = GetPageSizeCached();
  // Checking too large memory ranges is slow.
  CHECK_LT(size, page_size * 10);
  int sock_pair[2];
  if (pipe(sock_pair))
    return false;
  uptr bytes_written =
      internal_write(sock_pair[1], reinterpret_cast<void *>(beg), size);
  int write_errno;
  bool result;
  if (internal_iserror(bytes_written, &write_errno)) {
    CHECK_EQ(EFAULT, write_errno);
    result = false;
  } else {
    result = (bytes_written == size);
  }
  internal_close(sock_pair[0]);
  internal_close(sock_pair[1]);
  return result;
}

void PrepareForSandboxing(__sanitizer_sandbox_arguments *args) {
  // Some kinds of sandboxes may forbid filesystem access, so we won't be able
  // to read the file mappings from /proc/self/maps. Luckily, neither the
  // process will be able to load additional libraries, so it's fine to use the
  // cached mappings.
  MemoryMappingLayout::CacheMemoryMappings();
  // Same for /proc/self/exe in the symbolizer.
#if !SANITIZER_GO
  Symbolizer::GetOrInit()->PrepareForSandboxing();
  CovPrepareForSandboxing(args);
#endif
}

#if SANITIZER_ANDROID
int GetNamedMappingFd(const char *name, uptr size) {
  return -1;
}
#else
int GetNamedMappingFd(const char *name, uptr size) {
  if (!common_flags()->decorate_proc_maps)
    return -1;
  char shmname[200];
  CHECK(internal_strlen(name) < sizeof(shmname) - 10);
  internal_snprintf(shmname, sizeof(shmname), "%zu [%s]", internal_getpid(),
                    name);
  int fd = shm_open(shmname, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
  CHECK_GE(fd, 0);
  int res = internal_ftruncate(fd, size);
  CHECK_EQ(0, res);
  res = shm_unlink(shmname);
  CHECK_EQ(0, res);
  return fd;
}
#endif

void *MmapFixedNoReserve(uptr fixed_addr, uptr size, const char *name) {
  int fd = name ? GetNamedMappingFd(name, size) : -1;
  unsigned flags = MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE;
  if (fd == -1) flags |= MAP_ANON;

  uptr PageSize = GetPageSizeCached();
  uptr p = internal_mmap((void *)(fixed_addr & ~(PageSize - 1)),
                         RoundUpTo(size, PageSize), PROT_READ | PROT_WRITE,
                         flags, fd, 0);
  int reserrno;
  if (internal_iserror(p, &reserrno))
    Report("ERROR: %s failed to "
           "allocate 0x%zx (%zd) bytes at address %zx (errno: %d)\n",
           SanitizerToolName, size, size, fixed_addr, reserrno);
  IncreaseTotalMmap(size);
  return (void *)p;
}

void *MmapNoAccess(uptr fixed_addr, uptr size, const char *name) {
  int fd = name ? GetNamedMappingFd(name, size) : -1;
  unsigned flags = MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE;
  if (fd == -1) flags |= MAP_ANON;

  return (void *)internal_mmap((void *)fixed_addr, size, PROT_NONE, flags, fd,
                               0);
}

}  // namespace __sanitizer

#endif  // SANITIZER_POSIX
