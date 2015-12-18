// Check that when having a DYLD_ROOT_PATH set, the symbolizer still works.
// RUN: env DYLD_ROOT_PATH="/" ASAN_OPTIONS=$ASAN_OPTIONS:verbosity=2 ASAN_SYMBOLIZER_PATH=$(which atos) \
// RUN:   not %run %t 2>&1 | FileCheck %s
//
// Due to a bug in atos, this only works on x86_64.
// REQUIRES: x86_64

#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  char *x = (char*)malloc(10 * sizeof(char));
  memset(x, 0, 10);
  int res = x[argc];
  free(x);
  free(x + argc - 1);  // BOOM
  // CHECK: AddressSanitizer: attempting double-free{{.*}}in thread T0
  // CHECK: Using atos at user-specified path:
  // CHECK: #0 0x{{.*}} in {{.*}}free
  // CHECK: #1 0x{{.*}} in main {{.*}}atos-symbolizer.cc:[[@LINE-4]]
  // CHECK: freed by thread T0 here:
  // CHECK: #0 0x{{.*}} in {{.*}}free
  // CHECK: #1 0x{{.*}} in main {{.*}}atos-symbolizer.cc:[[@LINE-8]]
  // CHECK: allocated by thread T0 here:
  // CHECK: atos-symbolizer.cc:[[@LINE-13]]
  return res;
}
