// RUN: %clang_cc1 -std=c++11 -fcilkplus -fsyntax-only -verify %s
// REQUIRES: cilkplus

constexpr int stride() { return 1; }

void f1() {
  int j = 0;
  const int k = 10;

  _Cilk_for (auto i = 0; i < 10; ++i); // OK

  _Cilk_for (decltype(j) i = 0; i < 10; ++i); // OK

  _Cilk_for (decltype(k) i = 0; i < 10; i++); // expected-error {{cannot assign to variable 'i' with const-qualified type}} \
                                              // expected-note {{variable 'i' declared const here}}

  _Cilk_for (int &i = j; i < 10; ++i); // expected-error {{loop control variable must have an integral, pointer, or class type in '_Cilk_for'}}

  _Cilk_for (int &&i = 0; i < 10; ++i); // expected-error {{loop control variable must have an integral, pointer, or class type in '_Cilk_for'}}

  _Cilk_for (auto x = bar(); j < 10; j++); //expected-error {{use of undeclared identifier 'bar'}}

  _Cilk_for (int i = 0; i < 10; i -= stride()); //expected-error {{loop increment is inconsistent with condition in '_Cilk_for': expected positive stride}} \
                                                //expected-note {{constant stride is -1}}
}

class NotAnInt {
};

enum AnEnum {
  EnumOne = 1
};

struct Base {
  Base();
  Base(int);
  bool operator<(int) const;
  bool operator>(int) const;
  bool operator<=(int) const;
  bool operator>=(int) const;
  bool operator!=(int) const;
  bool operator==(int) const;
  int operator-(int) const;
  int operator+(int) const;
  Base& operator++();   // prefix
  Base operator++(int); // postfix
  Base operator!() const;
  Base operator+=(int);
  Base operator+=(NotAnInt);
  Base operator-=(int);
  Base operator-=(NotAnInt);
};

int operator-(int, const Base &); // expected-note {{candidate function not viable}}
int operator+(int, const Base &);

struct DC : public Base {
  DC();
  DC(int);
};

struct NDC : public Base {
  NDC() = delete; // expected-note {{'NDC' has been explicitly marked deleted}}
  NDC(int);
};

struct NoLessThan : public Base {
  NoLessThan();
  NoLessThan(int);
  bool operator<(int) const = delete; // expected-note {{candidate function has been explicitly deleted}}
};

struct NoPreIncrement : public Base {
  NoPreIncrement();
  NoPreIncrement(int);
  NoPreIncrement& operator++() = delete; // expected-note {{candidate function has been explicitly deleted}}
};

struct NoCopyCtor : public Base {
  NoCopyCtor();
  NoCopyCtor(int);
  NoCopyCtor(const NoCopyCtor &) = delete; // expected-note {{'NoCopyCtor' has been explicitly marked deleted here}}
};

struct NoAddAssign : public Base {
  NoAddAssign();
  NoAddAssign(int);
  NoAddAssign operator+=(int) = delete; // expected-note {{candidate function has been explicitly deleted}}
};

void f2() {
  _Cilk_for (DC i; i < 10; ++i); // OK

  _Cilk_for (NDC i; i < 10; ++i); // expected-error {{call to deleted constructor of 'NDC'}}

  _Cilk_for (NoLessThan i; i < 10; ++i); // expected-error {{overload resolution selected deleted operator '<'}}

  _Cilk_for (NoPreIncrement i; i < 10; ++i); // expected-error {{overload resolution selected deleted operator '++'}}

  _Cilk_for (NoAddAssign i; i < 10; i++); // expected-error {{overload resolution selected deleted operator '+='}}
}

struct NoOps : public Base {
  bool operator<(int) const = delete; // expected-note {{candidate function has been explicitly deleted}}
  bool operator>(int) const = delete; // expected-note {{candidate function has been explicitly deleted}}
  bool operator<=(int) const = delete; // expected-note {{candidate function has been explicitly deleted}}
  bool operator>=(int) const = delete; // expected-note {{candidate function has been explicitly deleted}}
  bool operator!=(int) const = delete; // expected-note {{candidate function has been explicitly deleted}}
};

struct BadTy {
  BadTy();
  bool operator<(int) const;
  float operator-(int) const;
  BadTy& operator++();
};

struct A {
  bool operator<(int);
  A& operator+=(int);
  A& operator+=(float);
};
int operator-(int, const A&); // expected-note {{candidate function not viable: no known conversion from 'BadTy' to 'const A' for 2nd argument}}
void ops() {
  // Need to check these here, since they create a CXXOperatorCallExpr, rather
  // than a BinaryOperator.
  _Cilk_for (DC i; i < 10; ++i); // OK
  _Cilk_for (NoOps i; i < 10; ++i); // expected-error {{overload resolution selected deleted operator '<'}}

  _Cilk_for (DC i; i <= 10; ++i); // OK
  _Cilk_for (NoOps i; i <= 10; ++i); // expected-error {{overload resolution selected deleted operator '<='}}

  _Cilk_for (DC i; i > 10; ++i); // expected-error {{loop increment is inconsistent with condition in '_Cilk_for': expected negative stride}} \
                                 // expected-note  {{constant stride is 1}}
  _Cilk_for (NoOps i; i > 10; ++i); // expected-error {{overload resolution selected deleted operator '>'}}

  _Cilk_for (DC i; i >= 10; ++i); // expected-error {{loop increment is inconsistent with condition in '_Cilk_for': expected negative stride}} \
                                  // expected-note  {{constant stride is 1}}
  _Cilk_for (NoOps i; i >= 10; ++i); // expected-error {{overload resolution selected deleted operator '>='}}

  _Cilk_for (DC i; i != 10; ++i); // OK
  _Cilk_for (NoOps i; i != 10; ++i);  // expected-error {{overload resolution selected deleted operator '!='}}

  _Cilk_for (DC i; i == 10; ++i); // expected-error {{loop condition operator must be one of '<', '<=', '>', '>=', or '!=' in '_Cilk_for'}}
  _Cilk_for (NoOps i; i == 10; ++i); // expected-error {{loop condition operator must be one of '<', '<=', '>', '>=', or '!=' in '_Cilk_for'}}

  _Cilk_for (BadTy i; i < 10; ++i); // expected-error {{end - begin must be well-formed in '_Cilk_for'}} \
                                    // expected-error {{invalid operands to binary expression ('int' and 'BadTy')}} \
                                    // expected-note {{loop begin expression here}} \
                                    // expected-note {{loop end expression here}}

  // Increment related tests

  _Cilk_for (DC i; i < 10; !i); // expected-error {{loop increment operator must be one of operators '++', '--', '+=', or '-=' in '_Cilk_for'}}

  _Cilk_for (int i = 0; i < 10; ); // expected-error {{missing loop increment expression in '_Cilk_for'}}

  _Cilk_for (DC i; i < 10; i += 2); // OK
  _Cilk_for (DC i; i < 10; i += EnumOne); // OK
  _Cilk_for (DC i; i < 10; i += NotAnInt()); // expected-error {{right-hand side of '+=' must have integral or enum type in '_Cilk_for' increment}}
  _Cilk_for (DC i; i < 10; i -= 2); // expected-error {{loop increment is inconsistent with condition in '_Cilk_for': expected positive stride}} \
                                    // expected-note  {{constant stride is -2}}
  _Cilk_for (DC i; i < 10; i -= EnumOne); // expected-error {{loop increment is inconsistent with condition in '_Cilk_for': expected positive stride}} \
                                          // expected-note  {{constant stride is -1}}
  _Cilk_for (DC i; i < 10; i -= NotAnInt()); // expected-error {{right-hand side of '-=' must have integral or enum type in '_Cilk_for' increment}}

  _Cilk_for (DC i; i < 10; (0, ++i)); // expected-warning {{expression result unused}} expected-error {{loop increment operator must be one of operators '++', '--', '+=', or '-=' in '_Cilk_for'}}

  _Cilk_for (DC i; i < 10; (i += 2)); // OK
  _Cilk_for (DC i; i < 10; ((i += 2))); // OK
  _Cilk_for (DC i; i < 10; (i += NotAnInt())); // expected-error {{right-hand side of '+=' must have integral or enum type in '_Cilk_for' increment}}

  struct S {
    S();
    ~S();
    operator int() const;
  };
  _Cilk_for (int i = 0; i < 10; i += S()); // OK

  _Cilk_for (A a; a < 100; a += 1); // OK
}

struct Bool {
  operator bool() const; // expected-note {{conversion to type 'bool' declared here}}
};
struct C : public Base { };
Bool operator<(const C&, int);
int operator-(int, const C&);

struct BoolWithCleanup {
  ~BoolWithCleanup(); // make the condition into an ExpressionWithCleanups
  operator bool() const; // expected-note {{conversion to type 'bool' declared here}}
};

struct D : public Base { };
BoolWithCleanup operator<(const D&, int);
int operator-(int, const D&);

struct From {
  bool operator++(int);
};
int operator-(int, const From&);
struct To : public Base {
  To();
  To(const From&);
};
bool operator<(const To&, int);

struct ToInt : public Base {
  int operator<(int);
};

struct ToPtr: public Base {
  void* operator<(int);
};

struct ToRef: public Base {
  bool& operator<(int);
};

struct ToCRef: public Base {
  const bool& operator<(int);
};

void conversions() {
  _Cilk_for (C c; c < 5; c++); // expected-warning {{user-defined conversion from 'Bool' to 'bool' will not be used when calculating the number of iterations in '_Cilk_for'}}
  _Cilk_for (D d; d < 5; d++); // expected-warning {{user-defined conversion from 'BoolWithCleanup' to 'bool' will not be used when calculating the number of iterations in '_Cilk_for'}}
  _Cilk_for (From c; c < 5; c++); // expected-error {{no viable overloaded '+='}}
  _Cilk_for (ToInt c; c < 5; c++); // OK
  _Cilk_for (ToPtr c; c < 5; c++); // OK
  _Cilk_for (ToRef c; c < 5; c++); // OK
  _Cilk_for (ToCRef c; c < 5; c++); // OK
}

int jump() {
  _Cilk_for (int i = 0; i < 10; ++i) {
    return 0; // expected-error {{cannot return from within a '_Cilk_for' loop}}
  }

  _Cilk_for (int i = 0; i < 10; ++i) {
    []() { return 0; }(); // OK
  }

  NoCopyCtor j;
  _Cilk_for (NoCopyCtor i(j); i < 10; ++i); // expected-error {{call to deleted constructor of 'NoCopyCtor'}}
}

namespace ns_member {

struct B {
  int val;
  B(int v = 0);
  operator int&();
};

struct Derived : public B {
  Derived(int v = 0) : B(v) {}
};

void test() {
  _Cilk_for (Derived i; i < 10; ++i); // OK
  _Cilk_for (Derived i; i.operator int&() < 10; ++i); // expected-error {{loop condition does not test control variable 'i' in '_Cilk_for'}} \
                                                      // expected-note {{allowed forms are 'i' OP expr, and expr OP 'i'}}
  _Cilk_for (Derived i; i.val < 10; ++i); // expected-error {{loop condition does not test control variable 'i' in '_Cilk_for'}} \
                                          // expected-note {{allowed forms are 'i' OP expr, and expr OP 'i'}}
  _Cilk_for (Derived i; i < 10; i.operator int&()++); // expected-error {{loop increment does not modify control variable 'i' in '_Cilk_for'}}
}

} // namespace

struct MyInt {
  operator int();
};

constexpr int grainsize(int x) {
  return 2 * x;
}

void test_grainsize() {
  MyInt gs;
  // MyInt is convertible to int and should work as grainsize.
  #pragma cilk grainsize = gs // OK
  _Cilk_for(int i = 0; i < 100; ++i);

  #pragma cilk grainsize = grainsize(0) // OK
  _Cilk_for(int i = 0; i < 100; ++i);

  #pragma cilk grainsize = grainsize(-1) // expected-error {{the behavior of Cilk for is unspecified for a negative grainsize}}
  _Cilk_for(int i = 0; i < 100; ++i);

  #pragma cilk grainsize = alignof(int) // OK
  _Cilk_for(int i = 0; i < 100; ++i);
}

namespace ns_bad_increment {

struct X { operator int(); };
int operator++(const X &, int);

void foo() {
  X x;
  _Cilk_for (int i = 0; i < 10; x++); // expected-error {{loop increment does not modify control variable 'i' in '_Cilk_for'}}
}

} // namespace

namespace union_variable {
union u {
  int a; float b;
  u(int i) : a(i) {}
};

bool operator< (u U, int i);
int operator++(u &U, int i);
int operator-(int i, u U);
u operator+=(u &U, int i);

void foo() {
  _Cilk_for (u x = 1; x < 100; x++); // OK
  // expected-note@+1 {{allowed forms are 'x' OP expr, and expr OP 'x'}}
  _Cilk_for (u x = 1; x.a < 100; x++); // expected-error {{loop condition does not test control variable 'x' in '_Cilk_for'}}
  _Cilk_for (u x = 1; x < 100; x.a++); // expected-error {{loop increment does not modify control variable 'x' in '_Cilk_for'}}
}
} // namespace

namespace ns_syntax_limit {

struct T { T(); ~T(); };
struct R { R(); ~R(); };

struct S {
  S(const S&);
  S(T);
  S(R, T t = T());
  ~S();
  operator T() {return T();}
  void operator++();
  void operator+=(int);
};

bool operator<(S, S);
int operator-(T, S);
int operator-(R, S);

void loop(S begin, T end1, R end2) {
  _Cilk_for (S i = begin; i < end1; ++i); // OK
  _Cilk_for (S i = begin; i < end2; ++i); // OK
}

} // namespace

void test_cilk_spawn_in_cilk_for() {
  extern int foo();
  extern int bar(int);
  _Cilk_for (int i = 0; i < 10; ++i)
    int j = _Cilk_spawn foo(), k = j, x = _Cilk_spawn bar(k); // OK

  _Cilk_for (int i = 0; i < 10; ++i)
    if (_Cilk_spawn foo()) // expected-error {{_Cilk_spawn is not at statement level}}
      _Cilk_spawn bar(i);
}
