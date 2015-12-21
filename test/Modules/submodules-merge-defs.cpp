// RUN: rm -rf %t
// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules-cache-path=%t -fmodules -fimplicit-module-maps -I %S/Inputs/submodules-merge-defs %s -verify -fno-modules-error-recovery -DTEXTUAL
// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules-cache-path=%t -fmodules -fimplicit-module-maps -I %S/Inputs/submodules-merge-defs %s -verify -fno-modules-error-recovery
// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules-cache-path=%t -fmodules -fimplicit-module-maps -I %S/Inputs/submodules-merge-defs %s -verify -fno-modules-error-recovery -fmodules-local-submodule-visibility -DTEXTUAL
// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules-cache-path=%t -fmodules -fimplicit-module-maps -I %S/Inputs/submodules-merge-defs %s -verify -fno-modules-error-recovery -fmodules-local-submodule-visibility
// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules-cache-path=%t -fimplicit-module-maps -I %S/Inputs/submodules-merge-defs %s -verify -fno-modules-error-recovery -fmodules-local-submodule-visibility -DTEXTUAL -DEARLY_INDIRECT_INCLUDE -fno-modules-hide-internal-linkage
// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules-cache-path=%t -fmodules -fimplicit-module-maps -I %S/Inputs/submodules-merge-defs %s -verify -fno-modules-error-recovery -fmodules-local-submodule-visibility -fmodule-feature use_defs_twice -DIMPORT_USE_2

// Trigger import of definitions, but don't make them visible.
#include "empty.h"
#ifdef EARLY_INDIRECT_INCLUDE
#include "indirect.h"
#endif

A pre_a; // expected-error {{must use 'struct'}}
#ifdef IMPORT_USE_2
// expected-error-re@-2 {{must be imported from one of {{.*}}stuff.use{{.*}}stuff.use-2}}
#elif EARLY_INDIRECT_INCLUDE
// expected-error@-4 {{must be imported from module 'merged-defs'}}
#else
// expected-error@-6 {{must be imported from module 'stuff.use'}}
#endif
// expected-note@defs.h:1 +{{here}}
// expected-note@defs.h:2 +{{here}}
int pre_use_a = use_a(pre_a); // expected-error {{'A' must be imported}} expected-error {{'use_a' must be imported}}

B::Inner2 pre_bi; // expected-error +{{must be imported}}
// expected-note@defs.h:4 +{{here}}
// expected-note@defs.h:11 +{{here}}
void pre_bfi(B b) { // expected-error {{must use 'class'}} expected-error +{{must be imported}}
  b.f<int>(); // expected-error +{{must be imported}} expected-error +{{}}
  // expected-note@defs.h:12 +{{here}}
}

C_Base<1> pre_cb1; // expected-error +{{must be imported}}
// expected-note@defs.h:16 +{{here}}
C1 pre_c1; // expected-error +{{must be imported}} expected-error {{must use 'struct'}}
// expected-note@defs.h:18 +{{here}}
C2 pre_c2; // expected-error +{{must be imported}} expected-error {{must use 'struct'}}
// expected-note@defs.h:19 +{{here}}

D::X pre_dx; // expected-error +{{must be imported}}
// expected-note@defs.h:21 +{{here}}
// expected-note@defs.h:22 +{{here}}
// FIXME: We should warn that use_dx is being used without being imported.
int pre_use_dx = use_dx(pre_dx);

int pre_e = E(0); // expected-error {{must be imported}}
// expected-note@defs.h:25 +{{here}}

int pre_ff = F<int>().f(); // expected-error +{{must be imported}}
int pre_fg = F<int>().g<int>(); // expected-error +{{must be imported}}
// expected-note@defs.h:27 +{{here}}

G::A pre_ga // expected-error +{{must be imported}}
  = G::a; // expected-error +{{must be imported}}
// expected-note@defs.h:42 +{{here}}
// expected-note@defs.h:43 +{{here}}
decltype(G::h) pre_gh = G::h; // expected-error +{{must be imported}}
// expected-note@defs.h:44 +{{here}}

J<> pre_j; // expected-error {{declaration of 'J' must be imported}}
#ifdef IMPORT_USE_2
// expected-error-re@-2 {{default argument of 'J' must be imported from one of {{.*}}stuff.use{{.*}}stuff.use-2}}
#elif EARLY_INDIRECT_INCLUDE
// expected-error@-4 {{default argument of 'J' must be imported from module 'merged-defs'}}
#else
// expected-error@-6 {{default argument of 'J' must be imported from module 'stuff.use'}}
#endif
// expected-note@defs.h:51 +{{here}}

ScopedEnum pre_scopedenum; // expected-error {{must be imported}} expected-error {{must use 'enum'}}
// expected-note@defs.h:99 {{here}}
enum ScopedEnum : int;
ScopedEnum pre_scopedenum_declared; // ok

// Make definitions from second module visible.
#ifdef TEXTUAL
#include "import-and-redefine.h"
#elif defined IMPORT_USE_2
#include "use-defs-2.h"
#else
#include "merged-defs.h"
#endif

A post_a;
int post_use_a = use_a(post_a);
B::Inner2 post_bi;
void post_bfi(B b) {
  b.f<int>();
}
C_Base<1> post_cb1;
C1 c1;
C2 c2;
D::X post_dx;
int post_use_dx = use_dx(post_dx);
int post_e = E(0);
int post_ff = F<char>().f();
int post_fg = F<char>().g<int>();
G::A post_ga = G::a;
decltype(G::h) post_gh = G::h;
J<> post_j;
template<typename T, int N, template<typename> class K> struct J;
J<> post_j2;
FriendDefArg::Y<int> friend_def_arg;
FriendDefArg::D<> friend_def_arg_d;

MergeFunctionTemplateSpecializations::X<int>::Q<char> xiqc;

#ifdef TEXTUAL
#include "use-defs.h"
void use_static_inline() { StaticInline::g({}); }
#endif
