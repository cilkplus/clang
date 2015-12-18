// Minimal test for LeakSanitizer+AddressSanitizer.
// REQUIRES: leak-detection
//
// RUN: %clangxx_asan  %s -o %t
// RUN: env ASAN_OPTIONS=$ASAN_OPTIONS:detect_leaks=1 not %run %t  2>&1 | FileCheck %s
// RUN: env ASAN_OPTIONS=$ASAN_OPTIONS                not %run %t  2>&1 | FileCheck %s
// RUN: env ASAN_OPTIONS=$ASAN_OPTIONS:detect_leaks=0     %run %t
#include <stdio.h>
int *t;

int main(int argc, char **argv) {
  t = new int[argc - 1];
  printf("t: %p\n", t);
  t = 0;
}
// CHECK: LeakSanitizer: detected memory leaks
