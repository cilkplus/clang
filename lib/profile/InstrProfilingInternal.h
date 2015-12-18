/*===- InstrProfiling.h- Support library for PGO instrumentation ----------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#ifndef PROFILE_INSTRPROFILING_INTERNALH_
#define PROFILE_INSTRPROFILING_INTERNALH_

#include "InstrProfiling.h"

/*!
 * \brief Write instrumentation data to the given buffer, given explicit
 * pointers to the live data in memory.  This function is probably not what you
 * want.  Use __llvm_profile_get_size_for_buffer instead.  Use this function if
 * your program has a custom memory layout.
 */
uint64_t __llvm_profile_get_size_for_buffer_internal(
    const __llvm_profile_data *DataBegin, const __llvm_profile_data *DataEnd,
    const uint64_t *CountersBegin, const uint64_t *CountersEnd,
    const char *NamesBegin, const char *NamesEnd);

/*!
 * \brief Write instrumentation data to the given buffer, given explicit
 * pointers to the live data in memory.  This function is probably not what you
 * want.  Use __llvm_profile_write_buffer instead.  Use this function if your
 * program has a custom memory layout.
 *
 * \pre \c Buffer is the start of a buffer at least as big as \a
 * __llvm_profile_get_size_for_buffer_internal().
 */
int __llvm_profile_write_buffer_internal(
    char *Buffer, const __llvm_profile_data *DataBegin,
    const __llvm_profile_data *DataEnd, const uint64_t *CountersBegin,
    const uint64_t *CountersEnd, const char *NamesBegin, const char *NamesEnd);

#endif
