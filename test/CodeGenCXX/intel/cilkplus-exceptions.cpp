// RUN: %clang_cc1 -std=c++11 -fcxx-exceptions -fexceptions -fcilkplus -emit-llvm %s -o %t
// RUN: FileCheck -check-prefix=CHECK_PARENT --input-file=%t %s
// RUN: FileCheck -check-prefix=CHECK_HELPER_F1 --input-file=%t %s
// RUN: FileCheck -check-prefix=CHECK_HELPER_F2 --input-file=%t %s
// RUN: %clang_cc1 -disable-llvm-optzns -std=c++11 -fcxx-exceptions -fexceptions -fcilkplus -emit-llvm %s -o %t-noopt
// RUN: FileCheck -check-prefix=CHECK_IMPLICIT_SYNC --input-file=%t-noopt %s
// RUN: FileCheck -check-prefix=CHECK_SYNC_JUMP --input-file=%t-noopt %s
// RUN: FileCheck -check-prefix=CHECK_MISC_IMP_SYNC --input-file=%t-noopt %s
// RUN: FileCheck -check-prefix=CHECK_INIT --input-file=%t-noopt %s
// REQUIRES: cilkplus
// XFAIL: win

namespace stack_frame_cleanup {
  extern void touch();

  struct C {
    C() { touch(); }
    ~C(){ touch(); }
  };

  template <typename T> void f1(T);
  template <typename T> T f2(T);

  template <typename T, int x>
  void test_f1() {
    _Cilk_spawn f1(T());
  }

  template <typename T, int x>
  void test_f2(T &ret) {
    ret = _Cilk_spawn f2(T());
  }

  void parent_stack_frame_test() {
    test_f1<int, 23>();
    // CHECK_PARENT: define {{.*}}@{{.*}}test_f1
    // CHECK_PARENT: alloca %__cilkrts_stack_frame
    // CHECK_PARENT-NEXT: call void @__cilk_parent_prologue
    // CHECK_PARENT: invoke void @__cilk_spawn_helper
    //
    // * Normal exit *
    //
    // CHECK_PARENT: call void @__cilk_parent_epilogue
    // CHECK_PARENT-NEXT: ret void
    //
    // * Exit due to exception *
    //
    // CHECK_PARENT: call void @__cilk_parent_epilogue
    // CHECK_PARENT-NEXT: br label
  }

  void helper_stack_frame_test() {
    test_f1<C, 29>();
    // CHECK_HELPER_F1: define {{.*}}@{{.*}}helper_stack_frame_test
    // CHECK_HELPER_F1:   invoke void @[[Helper:__cilk_spawn_helper[0-9]*]]
    //
    // CHECK_HELPER_F1: define internal void @[[Helper]]
    // CHECK_HELPER_F1: alloca %__cilkrts_stack_frame
    // CHECK_HELPER_F1: call void @__cilk_reset_worker
    //
    // Call C's constructor
    // CHECK_HELPER_F1: invoke {{.*}}@{{.*}}stack_frame_cleanup
    //
    // CHECK_HELPER_F1: call void @__cilk_helper_prologue
    // CHECK_HELPER_F1-NEXT: invoke {{.*}}@{{.*}}stack_frame_cleanup
    //
    // * Normal exit *
    //
    // Call C's destructor
    // CHECK_HELPER_F1: call {{.*}}@{{.*}}stack_frame_cleanup
    // CHECK_HELPER_F1: call void @__cilk_helper_epilogue
    //
    // * Exit due to exception *
    //
    // CHECK_HELPER_F1: call {{.*}}@{{.*}}stack_frame_cleanup
    // CHECK_HELPER_F1-NEXT: br label
    // CHECK_HELPER_F1: call void @__cilk_helper_epilogue
  }

  void helper_check_assignment() {
    int x = 0;
    test_f2<int, 37>(x);
    // CHECK_HELPER_F2: define {{.*}}@{{.*}}helper_check_assignment
    // CHECK_HELPER_F2:   invoke void @[[Helper:__cilk_spawn_helper[0-9]*]]
    //
    // CHECK_HELPER_F2: define internal void @[[Helper]]
    // CHECK_HELPER_F2: [[REG:%[a-zA-Z0-9]+]] = getelementptr inbounds %struct
    // CHECK_HELPER_F2-NEXT: load i32*, i32** [[REG]]
    // CHECK_HELPER_F2-NEXT: call void @__cilk_helper_prologue
    // CHECK_HELPER_F2-NEXT: [[RET_REG:%[a-zA-Z0-9]+]] = invoke {{.*}}@{{.*}}stack_frame_cleanup
    //
    // * Normal exit *
    //
    // CHECK_HELPER_F2: store i32 [[RET_REG]]
    // CHECK_HELPER_F2: call void @__cilk_helper_epilogue
  }

  void foo();
  bool a;

  void test3() {
    try {
      _Cilk_spawn foo();
      if (a) {
        goto out;
      }
    } catch (...) {
    }
out: return;
    // CHECK_SYNC_JUMP: define {{.*}}@{{.*}}test3
    //
    // * Implicit sync while entering the try block
    //
    // CHECK_SYNC_JUMP: invoke void @__cilk_sync
    //
    // * Exit due to exception *
    //
    // CHECK_SYNC_JUMP: call void @__cilk_excepting_sync
    //
    // * All normal exits *
    // All normal exits go through a single cleanup block, which uses a switch
    // to determine which path to continue after the cleanup.
    //
    // CHECK_SYNC_JUMP: invoke void @__cilk_sync
  }

  void test4() {
    for (;;) {
      try {
        _Cilk_spawn foo();
        if (a) {
          break;
        }
      } catch (...) {
      }
    }
    // CHECK_SYNC_JUMP: define {{.*}}@{{.*}}test4
    //
    // * Implicit sync while entering the try block
    //
    // CHECK_SYNC_JUMP: invoke void @__cilk_sync
    //
    // * Exit due to exception *
    //
    // CHECK_SYNC_JUMP: call void @__cilk_excepting_sync
    //
    // * All normal exits *
    // All normal exits go through a single cleanup block, which uses a switch
    // to determine which path to continue after the cleanup.
    //
    // CHECK_SYNC_JUMP: invoke void @__cilk_sync
  }

  void test5() {
    for (;;) {
      try {
        _Cilk_spawn foo();
        if (a) {
          continue;
        }
      } catch (...) {
      }
    }
    // CHECK_SYNC_JUMP: define {{.*}}@{{.*}}test5
    //
    // * Implicit sync while entering the try block
    //
    // CHECK_SYNC_JUMP: invoke void @__cilk_sync
    //
    // * Exit due to exception *
    //
    // CHECK_SYNC_JUMP: call void @__cilk_excepting_sync
    //
    // * All normal exits *
    // All normal exits go through a single cleanup block, which uses a switch
    // to determine which path to continue after the cleanup.
    //
    // CHECK_SYNC_JUMP: invoke void @__cilk_sync
  }
}

namespace implicit_sync_elision_basic {

void foo();
void bar();

void test1_anchor() throw ();

// No implicit sync for the function
void test1() {
  try {
    _Cilk_spawn foo();
  } catch (...) {
    bar();
  }

  test1_anchor();
  // CHECK_IMPLICIT_SYNC: define {{.*}}@{{.*}}test1
  //
  // CHECK_IMPLICIT_SYNC: call {{.*}}@{{.*}}test1_anchor
  // CHECK_IMPLICIT_SYNC-NEXT: call void @__cilk_parent_epilogue
  // CHECK_IMPLICIT_SYNC-NEXT: ret void
}

void test2_anchor() throw ();

// Should have an implicit sync for the function
void test2() {
  try {
    foo();
  } catch (...) {
    _Cilk_spawn bar();
  }

  test2_anchor();
  // CHECK_IMPLICIT_SYNC: define {{.*}}@{{.*}}test2
  // CHECK_IMPLICIT_SYNC: call {{.*}}@{{.*}}test2_anchor
  // CHECK_IMPLICIT_SYNC-NEXT: invoke void @__cilk_sync
  // CHECK_IMPLICIT_SYNC: call void @__cilk_parent_epilogue
  // CHECK_IMPLICIT_SYNC-NEXT: ret void
}

void test3_anchor() throw ();

// Should have an implicit sync for the function
void test3() {
  try {
    _Cilk_spawn foo();
  } catch (...) {
    _Cilk_spawn bar();
  }

  test3_anchor();
  // CHECK_IMPLICIT_SYNC: define {{.*}}@{{.*}}test3
  // CHECK_IMPLICIT_SYNC: call {{.*}}@{{.*}}test3_anchor
  // CHECK_IMPLICIT_SYNC-NEXT: invoke void @__cilk_sync
  // CHECK_IMPLICIT_SYNC: call void @__cilk_parent_epilogue
  // CHECK_IMPLICIT_SYNC-NEXT: ret void
}

void test4_anchor() throw ();

// No implicit sync for the function
void test4() {
  try {
    try {
      _Cilk_spawn foo();
    } catch (...) {
      _Cilk_spawn bar();
    }
  } catch (...) {
    bar();
  }

  test4_anchor();
  // CHECK_IMPLICIT_SYNC: define {{.*}}@{{.*}}test4
  // CHECK_IMPLICIT_SYNC: call void {{.*}}@{{.*}}test4_anchor
  // CHECK_IMPLICIT_SYNC-NEXT: call void @__cilk_parent_epilogue
  // CHECK_IMPLICIT_SYNC-NEXT: ret void
}

// No implicit sync exiting try-block
void test5() {
  try {
    foo();
  } catch (...) {
    _Cilk_spawn bar();
  }
  // CHECK_IMPLICIT_SYNC: define {{.*}}@{{.*}}test5
  // CHECK_IMPLICIT_SYNC: invoke {{.*}}@{{.*}}foo
  // CHECK_IMPLICIT_SYNC-NOT: call void @__cilk_sync
  // CHECK_IMPLICIT_SYNC: call i8* @__cxa_begin_catch
}

void test6_anchor() throw ();

// No implicit sync for the outer try
void test6() {
  try {
    foo();
    try {
      _Cilk_spawn foo();
    } catch (...) {
      bar();
    }
    test6_anchor();
  } catch (...) {
    _Cilk_spawn bar();
    bar();
  }
  // CHECK_IMPLICIT_SYNC: define {{.*}}@{{.*}}test6
  // CHECK_IMPLICIT_SYNC: call {{.*}}@{{.*}}test6_anchor
  // CHECK_IMPLICIT_SYNC-NEXT: br
}
} // namespace

namespace misc {

void foo() throw();
void bar() throw();
void baz() throw();

void entering_any_try_block() {
  _Cilk_spawn foo();

  try { bar(); } catch (...) { }

  // CHECK_MISC_IMP_SYNC: define {{.*}}@{{.*}}entering_any_try_block
  // CHECK_MISC_IMP_SYNC: invoke void @__cilk_sync
  // CHECK_MISC_IMP_SYNC: call {{.*}}@{{.*}}bar
}

void entering_spawning_try_block() {
  try { foo(); _Cilk_spawn bar(); } catch (...) { }

  // CHECK_MISC_IMP_SYNC: define {{.*}}@{{.*}}entering_spawning_try_block
  // CHECK_MISC_IMP_SYNC: invoke void @__cilk_sync
  // CHECK_MISC_IMP_SYNC: call {{.*}}@{{.*}}foo
}

void entering_nested_try_block() {
  _Cilk_spawn foo();

  try {
    bar();
    try { baz(); } catch (...) { }
  } catch (...) { }

  // CHECK_MISC_IMP_SYNC: define {{.*}}@{{.*}}entering_nested_try_block
  // CHECK_MISC_IMP_SYNC: invoke void @__cilk_sync
  // CHECK_MISC_IMP_SYNC: call {{.*}}@{{.*}}bar
  // CHECK_MISC_IMP_SYNC-NEXT: invoke void @__cilk_sync
  // CHECK_MISC_IMP_SYNC: call {{.*}}@{{.*}}baz
}

namespace spawn_variable_initialization {

struct Class {
  Class();
  ~Class();
};

Class makeClass();

void maybeThrow();

void test_value() {
  try {
    Class c = _Cilk_spawn makeClass();
  } catch (...) { }
  // CHECK_INIT: define {{.*}}@{{.*}}test_value
  // CHECK_INIT: invoke void @__cilk_spawn_helper
  // CHECK_INIT-NOT: ret
  //
  // Normal exit:
  // CHECK_INIT: call void @{{.*}}ClassD1
  // CHECK_INIT-NOT: ret
  //
  // Exceptional exit:
  // CHECK_INIT: call void @{{.*}}ClassD1
  // CHECK_INIT: ret
}

void test_rvalue_ref() {
  try {
    Class &&c = _Cilk_spawn makeClass();
    maybeThrow();
  } catch (...) { }
  // CHECK_INIT: define {{.*}}@{{.*}}test_rvalue_ref
  // CHECK_INIT: invoke void @__cilk_spawn_helper
  // CHECK_INIT-NOT: ret
  //
  // CHECK_INIT: invoke void @{{.*}}spawn_variable_initialization{{.*}}maybeThrow
  //
  // Normal exit:
  // CHECK_INIT: call void @{{.*}}ClassD1
  // CHECK_INIT-NOT: ret
  //
  // Exceptional exit:
  // CHECK_INIT: call void @{{.*}}ClassD1
  // CHECK_INIT: ret
}

void test_const_ref() {
  try {
    const Class &c = _Cilk_spawn makeClass();
    maybeThrow();
  } catch (...) { }
  // CHECK_INIT: define {{.*}}@{{.*}}test_const_ref
  // CHECK_INIT: invoke void @__cilk_spawn_helper
  // CHECK_INIT-NOT: ret
  //
  // CHECK_INIT: invoke void @{{.*}}spawn_variable_initialization{{.*}}maybeThrow
  //
  // Normal exit:
  // CHECK_INIT: call void @{{.*}}ClassD1
  // CHECK_INIT-NOT: ret
  //
  // Exceptional exit:
  // CHECK_INIT: call void @{{.*}}ClassD1Ev
  // CHECK_INIT: ret
}

// If the spawn itself fails, don't call the destructor
void test_no_destruct_uninitialized() {
  try {
    Class &&c = _Cilk_spawn makeClass();
  } catch (...) { }
  // CHECK_INIT: define {{.*}}@{{.*}}test_no_destruct_uninitialized
  // CHECK_INIT: invoke void @__cilk_spawn_helper
  // CHECK_INIT-NOT: ret
  //
  // Normal exit:
  // CHECK_INIT: call void @{{.*}}ClassD1
  // CHECK_INIT-NOT: ret
  //
  // Exceptional exit:
  // CHECK_INIT-NOT: call void @{{.*}}ClassD1
  // CHECK_INIT: ret
}

struct Base {
  virtual ~Base();
};
struct Derived : public Base {
  ~Derived();
};

Derived makeDerived();

void test_bind_to_base_type() {
  try {
    Base &&c = _Cilk_spawn makeDerived();
  } catch (...) { }
  // CHECK_INIT: define {{.*}}@{{.*}}test_bind_to_base_type
  // CHECK_INIT: alloca {{.*}}Base"*
  // CHECK_INIT: alloca {{.*}}Derived"
  // CHECK_INIT: invoke void @__cilk_spawn_helper
  // CHECK_INIT-NOT: ret
  //
  // CHECK_INIT: call void @{{.*}}DerivedD1
}

} // namespace spawn_variable_initialization

} // namespace
