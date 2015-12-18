// RUN: %clang_asan -O2 %s -o %t
// RUN: env ASAN_OPTIONS=$ASAN_OPTIONS:check_printf=1 not %run %t 2>&1 | FileCheck --check-prefix=CHECK-ON %s
// RUN: env ASAN_OPTIONS=$ASAN_OPTIONS:check_printf=0 %run %t 2>&1 | FileCheck --check-prefix=CHECK-OFF %s
// RUN: not %run %t 2>&1 | FileCheck --check-prefix=CHECK-ON %s

// FIXME: printf is not intercepted on Windows yet.
// XFAIL: win32

#include <stdio.h>
int main() {
  volatile char c = '0';
  volatile int x = 12;
  volatile float f = 1.239;
  volatile char s[] = "34";
  volatile int n[1];
  printf("%c %d %.3f %s%n\n", c, x, f, s, &n[1]);
  return 0;
  // Check that %n is sanitized.
  // CHECK-ON: stack-buffer-overflow
  // CHECK-ON-NOT: 0 12 1.239 34
  // CHECK-OFF: 0 12 1.239 34
}
