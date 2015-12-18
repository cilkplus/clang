//===-- sanitizer_symbolizer_mac.cc ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries.
//
// Implementation of Mac-specific "atos" symbolizer.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_MAC

#include "sanitizer_allocator_internal.h"
#include "sanitizer_mac.h"
#include "sanitizer_symbolizer_mac.h"

namespace __sanitizer {

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

bool DlAddrSymbolizer::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  Dl_info info;
  int result = dladdr((const void *)addr, &info);
  if (!result) return false;
  const char *demangled = DemangleCXXABI(info.dli_sname);
  stack->info.function = internal_strdup(demangled);
  return true;
}

bool DlAddrSymbolizer::SymbolizeData(uptr addr, DataInfo *info) {
  return false;
}

class AtosSymbolizerProcess : public SymbolizerProcess {
 public:
  explicit AtosSymbolizerProcess(const char *path, pid_t parent_pid)
      : SymbolizerProcess(path, /*use_forkpty*/ true),
        parent_pid_(parent_pid) {}

 private:
  bool ReachedEndOfOutput(const char *buffer, uptr length) const override {
    return (length >= 1 && buffer[length - 1] == '\n');
  }

  void ExecuteWithDefaultArgs(const char *path_to_binary) const override {
    char pid_str[16];
    internal_snprintf(pid_str, sizeof(pid_str), "%d", parent_pid_);
    if (GetMacosVersion() == MACOS_VERSION_MAVERICKS) {
      // On Mavericks atos prints a deprecation warning which we suppress by
      // passing -d. The warning isn't present on other OSX versions, even the
      // newer ones.
      execl(path_to_binary, path_to_binary, "-p", pid_str, "-d", (char *)0);
    } else {
      execl(path_to_binary, path_to_binary, "-p", pid_str, (char *)0);
    }
  }

  pid_t parent_pid_;
};

static const char *kAtosErrorMessages[] = {
  "atos cannot examine process",
  "unable to get permission to examine process",
  "An admin user name and password is required",
  "could not load inserted library",
  "architecture mismatch between analysis process",
};

static bool IsAtosErrorMessage(const char *str) {
  for (uptr i = 0; i < ARRAY_SIZE(kAtosErrorMessages); i++) {
    if (internal_strstr(str, kAtosErrorMessages[i])) {
      return true;
    }
  }
  return false;
}

static bool ParseCommandOutput(const char *str, SymbolizedStack *res) {
  // Trim ending newlines.
  char *trim;
  ExtractTokenUpToDelimiter(str, "\n", &trim);

  // The line from `atos` is in one of these formats:
  //   myfunction (in library.dylib) (sourcefile.c:17)
  //   myfunction (in library.dylib) + 0x1fe
  //   0xdeadbeef (in library.dylib) + 0x1fe
  //   0xdeadbeef (in library.dylib)
  //   0xdeadbeef

  if (IsAtosErrorMessage(trim)) {
    Report("atos returned an error: %s\n", trim);
    InternalFree(trim);
    return false;
  }

  const char *rest = trim;
  char *function_name;
  rest = ExtractTokenUpToDelimiter(rest, " (in ", &function_name);
  if (internal_strncmp(function_name, "0x", 2) != 0)
    res->info.function = function_name;
  else
    InternalFree(function_name);
  rest = ExtractTokenUpToDelimiter(rest, ") ", &res->info.module);

  if (rest[0] == '(') {
    rest++;
    rest = ExtractTokenUpToDelimiter(rest, ":", &res->info.file);
    char *extracted_line_number;
    rest = ExtractTokenUpToDelimiter(rest, ")", &extracted_line_number);
    res->info.line = internal_atoll(extracted_line_number);
    InternalFree(extracted_line_number);
  }

  InternalFree(trim);
  return true;
}

AtosSymbolizer::AtosSymbolizer(const char *path, LowLevelAllocator *allocator)
    : process_(new(*allocator) AtosSymbolizerProcess(path, getpid())) {}

bool AtosSymbolizer::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  if (!process_) return false;
  char command[32];
  internal_snprintf(command, sizeof(command), "0x%zx\n", addr);
  const char *buf = process_->SendCommand(command);
  if (!buf) return false;
  if (!ParseCommandOutput(buf, stack)) {
    process_ = nullptr;
    return false;
  }
  return true;
}

bool AtosSymbolizer::SymbolizeData(uptr addr, DataInfo *info) { return false; }

}  // namespace __sanitizer

#endif  // SANITIZER_MAC
