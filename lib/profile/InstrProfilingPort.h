/*===- InstrProfilingPort.h- Support library for PGO instrumentation ------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#ifndef PROFILE_INSTRPROFILING_PORT_H_
#define PROFILE_INSTRPROFILING_PORT_H_

#ifdef _MSC_VER
#define COMPILER_RT_ALIGNAS(x) __declspec(align(x))
#define COMPILER_RT_VISIBILITY
#define COMPILER_RT_WEAK __declspec(selectany)
#elif __GNUC__
#define COMPILER_RT_ALIGNAS(x) __attribute__((aligned(x)))
#define COMPILER_RT_VISIBILITY __attribute__((visibility("hidden")))
#define COMPILER_RT_WEAK __attribute__((weak))
#endif

#define COMPILER_RT_SECTION(Sect) __attribute__((section(Sect)))

#if COMPILER_RT_HAS_ATOMICS == 1
#ifdef _MSC_VER
#include <windows.h>
#if defined(_WIN64)
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  (InterlockedCompareExchange64((LONGLONG volatile *)Ptr, (LONGLONG)NewV,      \
                                (LONGLONG)OldV) == (LONGLONG)OldV)
#else
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  (InterlockedCompareExchange((LONG volatile *)Ptr, (LONG)NewV, (LONG)OldV) == \
   (LONG)OldV)
#endif
#else
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  __sync_bool_compare_and_swap(Ptr, OldV, NewV)
#endif
#else
#define COMPILER_RT_BOOL_CMPXCHG(Ptr, OldV, NewV)                              \
  BoolCmpXchg((void **)Ptr, OldV, NewV)
#endif

#define PROF_ERR(Format, ...)                                                  \
  if (GetEnvHook && GetEnvHook("LLVM_PROFILE_VERBOSE_ERRORS"))                 \
    fprintf(stderr, Format, __VA_ARGS__);

#if defined(__FreeBSD__) && defined(__i386__)

/* System headers define 'size_t' incorrectly on x64 FreeBSD (prior to
 * FreeBSD 10, r232261) when compiled in 32-bit mode.
 */
#define PRIu64 "llu"
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef uint32_t uintptr_t;
#elif defined(__FreeBSD__) && defined(__x86_64__)
#define PRIu64 "lu"
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long int uintptr_t;

#else /* defined(__FreeBSD__) && defined(__i386__) */

#include <inttypes.h>
#include <stdint.h>

#endif /* defined(__FreeBSD__) && defined(__i386__) */

#endif /* PROFILE_INSTRPROFILING_PORT_H_ */
