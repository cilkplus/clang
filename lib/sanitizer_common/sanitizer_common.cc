//===-- sanitizer_common.cc -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_allocator_internal.h"
#include "sanitizer_flags.h"
#include "sanitizer_libc.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_stacktrace_printer.h"
#include "sanitizer_symbolizer.h"

namespace __sanitizer {

const char *SanitizerToolName = "SanitizerTool";

atomic_uint32_t current_verbosity;

uptr GetPageSizeCached() {
  static uptr PageSize;
  if (!PageSize)
    PageSize = GetPageSize();
  return PageSize;
}

StaticSpinMutex report_file_mu;
ReportFile report_file = {&report_file_mu, kStderrFd, "", "", 0};

void RawWrite(const char *buffer) {
  report_file.Write(buffer, internal_strlen(buffer));
}

void ReportFile::ReopenIfNecessary() {
  mu->CheckLocked();
  if (fd == kStdoutFd || fd == kStderrFd) return;

  uptr pid = internal_getpid();
  // If in tracer, use the parent's file.
  if (pid == stoptheworld_tracer_pid)
    pid = stoptheworld_tracer_ppid;
  if (fd != kInvalidFd) {
    // If the report file is already opened by the current process,
    // do nothing. Otherwise the report file was opened by the parent
    // process, close it now.
    if (fd_pid == pid)
      return;
    else
      CloseFile(fd);
  }

  const char *exe_name = GetBinaryBasename();
  if (common_flags()->log_exe_name && exe_name) {
    internal_snprintf(full_path, kMaxPathLength, "%s.%s.%zu", path_prefix,
                      exe_name, pid);
  } else {
    internal_snprintf(full_path, kMaxPathLength, "%s.%zu", path_prefix, pid);
  }
  fd = OpenFile(full_path, WrOnly);
  if (fd == kInvalidFd) {
    const char *ErrorMsgPrefix = "ERROR: Can't open file: ";
    WriteToFile(kStderrFd, ErrorMsgPrefix, internal_strlen(ErrorMsgPrefix));
    WriteToFile(kStderrFd, full_path, internal_strlen(full_path));
    Die();
  }
  fd_pid = pid;
}

void ReportFile::SetReportPath(const char *path) {
  if (!path)
    return;
  uptr len = internal_strlen(path);
  if (len > sizeof(path_prefix) - 100) {
    Report("ERROR: Path is too long: %c%c%c%c%c%c%c%c...\n",
           path[0], path[1], path[2], path[3],
           path[4], path[5], path[6], path[7]);
    Die();
  }

  SpinMutexLock l(mu);
  if (fd != kStdoutFd && fd != kStderrFd && fd != kInvalidFd)
    CloseFile(fd);
  fd = kInvalidFd;
  if (internal_strcmp(path, "stdout") == 0) {
    fd = kStdoutFd;
  } else if (internal_strcmp(path, "stderr") == 0) {
    fd = kStderrFd;
  } else {
    internal_snprintf(path_prefix, kMaxPathLength, "%s", path);
  }
}

// PID of the tracer task in StopTheWorld. It shares the address space with the
// main process, but has a different PID and thus requires special handling.
uptr stoptheworld_tracer_pid = 0;
// Cached pid of parent process - if the parent process dies, we want to keep
// writing to the same log file.
uptr stoptheworld_tracer_ppid = 0;

static DieCallbackType InternalDieCallback, UserDieCallback;
void SetDieCallback(DieCallbackType callback) {
  InternalDieCallback = callback;
}
void SetUserDieCallback(DieCallbackType callback) {
  UserDieCallback = callback;
}

DieCallbackType GetDieCallback() {
  return InternalDieCallback;
}

void NORETURN Die() {
  if (UserDieCallback)
    UserDieCallback();
  if (InternalDieCallback)
    InternalDieCallback();
  internal__exit(1);
}

static CheckFailedCallbackType CheckFailedCallback;
void SetCheckFailedCallback(CheckFailedCallbackType callback) {
  CheckFailedCallback = callback;
}

void NORETURN CheckFailed(const char *file, int line, const char *cond,
                          u64 v1, u64 v2) {
  if (CheckFailedCallback) {
    CheckFailedCallback(file, line, cond, v1, v2);
  }
  Report("Sanitizer CHECK failed: %s:%d %s (%lld, %lld)\n", file, line, cond,
                                                            v1, v2);
  Die();
}

uptr ReadFileToBuffer(const char *file_name, char **buff, uptr *buff_size,
                      uptr max_len, error_t *errno_p) {
  uptr PageSize = GetPageSizeCached();
  uptr kMinFileLen = PageSize;
  uptr read_len = 0;
  *buff = 0;
  *buff_size = 0;
  // The files we usually open are not seekable, so try different buffer sizes.
  for (uptr size = kMinFileLen; size <= max_len; size *= 2) {
    fd_t fd = OpenFile(file_name, RdOnly, errno_p);
    if (fd == kInvalidFd) return 0;
    UnmapOrDie(*buff, *buff_size);
    *buff = (char*)MmapOrDie(size, __func__);
    *buff_size = size;
    // Read up to one page at a time.
    read_len = 0;
    bool reached_eof = false;
    while (read_len + PageSize <= size) {
      uptr just_read;
      if (!ReadFromFile(fd, *buff + read_len, PageSize, &just_read, errno_p)) {
        UnmapOrDie(*buff, *buff_size);
        return 0;
      }
      if (just_read == 0) {
        reached_eof = true;
        break;
      }
      read_len += just_read;
    }
    CloseFile(fd);
    if (reached_eof)  // We've read the whole file.
      break;
  }
  return read_len;
}

typedef bool UptrComparisonFunction(const uptr &a, const uptr &b);

template<class T>
static inline bool CompareLess(const T &a, const T &b) {
  return a < b;
}

void SortArray(uptr *array, uptr size) {
  InternalSort<uptr*, UptrComparisonFunction>(&array, size, CompareLess);
}

// We want to map a chunk of address space aligned to 'alignment'.
// We do it by maping a bit more and then unmaping redundant pieces.
// We probably can do it with fewer syscalls in some OS-dependent way.
void *MmapAlignedOrDie(uptr size, uptr alignment, const char *mem_type) {
// uptr PageSize = GetPageSizeCached();
  CHECK(IsPowerOfTwo(size));
  CHECK(IsPowerOfTwo(alignment));
  uptr map_size = size + alignment;
  uptr map_res = (uptr)MmapOrDie(map_size, mem_type);
  uptr map_end = map_res + map_size;
  uptr res = map_res;
  if (res & (alignment - 1))  // Not aligned.
    res = (map_res + alignment) & ~(alignment - 1);
  uptr end = res + size;
  if (res != map_res)
    UnmapOrDie((void*)map_res, res - map_res);
  if (end != map_end)
    UnmapOrDie((void*)end, map_end - end);
  return (void*)res;
}

const char *StripPathPrefix(const char *filepath,
                            const char *strip_path_prefix) {
  if (filepath == 0) return 0;
  if (strip_path_prefix == 0) return filepath;
  const char *res = filepath;
  if (const char *pos = internal_strstr(filepath, strip_path_prefix))
    res = pos + internal_strlen(strip_path_prefix);
  if (res[0] == '.' && res[1] == '/')
    res += 2;
  return res;
}

const char *StripModuleName(const char *module) {
  if (module == 0)
    return 0;
  if (SANITIZER_WINDOWS) {
    // On Windows, both slash and backslash are possible.
    // Pick the one that goes last.
    if (const char *bslash_pos = internal_strrchr(module, '\\'))
      return StripModuleName(bslash_pos + 1);
  }
  if (const char *slash_pos = internal_strrchr(module, '/')) {
    return slash_pos + 1;
  }
  return module;
}

void ReportErrorSummary(const char *error_message) {
  if (!common_flags()->print_summary)
    return;
  InternalScopedString buff(kMaxSummaryLength);
  buff.append("SUMMARY: %s: %s", SanitizerToolName, error_message);
  __sanitizer_report_error_summary(buff.data());
}

#ifndef SANITIZER_GO
void ReportErrorSummary(const char *error_type, const AddressInfo &info) {
  if (!common_flags()->print_summary)
    return;
  InternalScopedString buff(kMaxSummaryLength);
  buff.append("%s ", error_type);
  RenderFrame(&buff, "%L %F", 0, info, common_flags()->symbolize_vs_style,
              common_flags()->strip_path_prefix);
  ReportErrorSummary(buff.data());
}
#endif

void LoadedModule::set(const char *module_name, uptr base_address) {
  clear();
  full_name_ = internal_strdup(module_name);
  base_address_ = base_address;
}

void LoadedModule::clear() {
  InternalFree(full_name_);
  full_name_ = nullptr;
  while (!ranges_.empty()) {
    AddressRange *r = ranges_.front();
    ranges_.pop_front();
    InternalFree(r);
  }
}

void LoadedModule::addAddressRange(uptr beg, uptr end, bool executable) {
  void *mem = InternalAlloc(sizeof(AddressRange));
  AddressRange *r = new(mem) AddressRange(beg, end, executable);
  ranges_.push_back(r);
}

bool LoadedModule::containsAddress(uptr address) const {
  for (Iterator iter = ranges(); iter.hasNext();) {
    const AddressRange *r = iter.next();
    if (r->beg <= address && address < r->end)
      return true;
  }
  return false;
}

static atomic_uintptr_t g_total_mmaped;

void IncreaseTotalMmap(uptr size) {
  if (!common_flags()->mmap_limit_mb) return;
  uptr total_mmaped =
      atomic_fetch_add(&g_total_mmaped, size, memory_order_relaxed) + size;
  // Since for now mmap_limit_mb is not a user-facing flag, just kill
  // a program. Use RAW_CHECK to avoid extra mmaps in reporting.
  RAW_CHECK((total_mmaped >> 20) < common_flags()->mmap_limit_mb);
}

void DecreaseTotalMmap(uptr size) {
  if (!common_flags()->mmap_limit_mb) return;
  atomic_fetch_sub(&g_total_mmaped, size, memory_order_relaxed);
}

bool TemplateMatch(const char *templ, const char *str) {
  if (str == 0 || str[0] == 0)
    return false;
  bool start = false;
  if (templ && templ[0] == '^') {
    start = true;
    templ++;
  }
  bool asterisk = false;
  while (templ && templ[0]) {
    if (templ[0] == '*') {
      templ++;
      start = false;
      asterisk = true;
      continue;
    }
    if (templ[0] == '$')
      return str[0] == 0 || asterisk;
    if (str[0] == 0)
      return false;
    char *tpos = (char*)internal_strchr(templ, '*');
    char *tpos1 = (char*)internal_strchr(templ, '$');
    if (tpos == 0 || (tpos1 && tpos1 < tpos))
      tpos = tpos1;
    if (tpos != 0)
      tpos[0] = 0;
    const char *str0 = str;
    const char *spos = internal_strstr(str, templ);
    str = spos + internal_strlen(templ);
    templ = tpos;
    if (tpos)
      tpos[0] = tpos == tpos1 ? '$' : '*';
    if (spos == 0)
      return false;
    if (start && spos != str0)
      return false;
    start = false;
    asterisk = false;
  }
  return true;
}

static char binary_name_cache_str[kMaxPathLength];
static const char *binary_basename_cache_str;

const char *GetBinaryBasename() {
  return binary_basename_cache_str;
}

// Call once to make sure that binary_name_cache_str is initialized
void CacheBinaryName() {
  if (binary_name_cache_str[0] != '\0')
    return;
  ReadBinaryName(binary_name_cache_str, sizeof(binary_name_cache_str));
  binary_basename_cache_str = StripModuleName(binary_name_cache_str);
}

uptr ReadBinaryNameCached(/*out*/char *buf, uptr buf_len) {
  CacheBinaryName();
  uptr name_len = internal_strlen(binary_name_cache_str);
  name_len = (name_len < buf_len - 1) ? name_len : buf_len - 1;
  if (buf_len == 0)
    return 0;
  internal_memcpy(buf, binary_name_cache_str, name_len);
  buf[name_len] = '\0';
  return name_len;
}

}  // namespace __sanitizer

using namespace __sanitizer;  // NOLINT

extern "C" {
void __sanitizer_set_report_path(const char *path) {
  report_file.SetReportPath(path);
}

void __sanitizer_report_error_summary(const char *error_summary) {
  Printf("%s\n", error_summary);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_set_death_callback(void (*callback)(void)) {
  SetUserDieCallback(callback);
}
}  // extern "C"
