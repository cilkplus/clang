// RUN: %clang_cc1 -fcilkplus -emit-llvm -O0 -o - %s | FileCheck %s
// REQUIRES: cilkplus

// CHECK: %struct.anon = type { i32*, float**, i32* }
// CHECK: %struct.anon.0 = type { i32*, float** }
// CHECK: %struct.anon.1 = type { i32*, float**, i32* }
// CHECK: %struct.anon.2 = type { i32*, float**, i32* }
// CHECK: %struct.anon.3 = type { i32*, float** }
// CHECK: %struct.anon.4 = type { i32*, float**, i32* }
// CHECK: %struct.anon.5 = type { i32*, float**, i32* }
// CHECK: %struct.anon.6 = type { i32*, float**, i32* }
// CHECK: %struct.anon.7 = type { i32*, float** }
// CHECK: %struct.anon.8 = type { i32*, float**, i32* }
// CHECK: %struct.anon.9 = type { i32*, float**, i32* }
// CHECK: %struct.anon.80 = type { i32*, float**, i32* }
// CHECK: %struct.anon.81 = type { i32*, float**, i32* }
// CHECK: %struct.anon.82 = type { i32*, float**, i32* }
// CHECK: %struct.anon.83 = type { i32*, float**, i32* }
// CHECK: %struct.anon.84 = type { i32*, float**, i32* }
// CHECK: %struct.anon.85 = type { i32*, float**, i32* }
// CHECK: %struct.anon.86 = type { i32*, float**, i32* }
// CHECK: %struct.anon.87 = type { i32*, float** }

extern void touch(float x);

void test_nested_loop_2(float* vec, int n) {
  _Cilk_for (int i2 = 0; i2 < n; ++i2) {
    touch(vec[i2]);
    _Cilk_for (int i1 = 0; i1 < n; ++i1) {
      touch(vec[i1]);
    }
  }
}

void test_nested_loop_3(float* vec, int n) {
  _Cilk_for (int i3 = 0; i3 < n; ++i3) {
    touch(vec[i3]);
    _Cilk_for (int i2 = 0; i2 < n; ++i2) {
      touch(vec[i2]);
      _Cilk_for (int i1 = 0; i1 < n; ++i1) {
        touch(vec[i1]);
      }
    }
  }
}

void test_nested_loop_4(float* vec, int n) {
  _Cilk_for (int i4 = 0; i4 < n; ++i4) {
    touch(vec[i4]);
    _Cilk_for (int i3 = 0; i3 < n; ++i3) {
      touch(vec[i3]);
      _Cilk_for (int i2 = 0; i2 < n; ++i2) {
        touch(vec[i2]);
        _Cilk_for (int i1 = 0; i1 < n; ++i1) {
          touch(vec[i1]);
        }
      }
    }
  }
}

void test_nested_loop_5(float* vec, int n) {
  _Cilk_for (int i5 = 0; i5 < n; ++i5) {
    touch(vec[i5]);
    _Cilk_for (int i4 = 0; i4 < n; ++i4) {
      touch(vec[i4]);
      _Cilk_for (int i3 = 0; i3 < n; ++i3) {
        touch(vec[i3]);
        _Cilk_for (int i2 = 0; i2 < n; ++i2) {
          touch(vec[i2]);
          _Cilk_for (int i1 = 0; i1 < n; ++i1) {
            touch(vec[i1]);
          }
        }
      }
    }
  }
}

void test_nested_loop_6(float* vec, int n) {
  _Cilk_for (int i6 = 0; i6 < n; ++i6) {
    touch(vec[i6]);
    _Cilk_for (int i5 = 0; i5 < n; ++i5) {
      touch(vec[i5]);
      _Cilk_for (int i4 = 0; i4 < n; ++i4) {
        touch(vec[i4]);
        _Cilk_for (int i3 = 0; i3 < n; ++i3) {
          touch(vec[i3]);
          _Cilk_for (int i2 = 0; i2 < n; ++i2) {
            touch(vec[i2]);
            _Cilk_for (int i1 = 0; i1 < n; ++i1) {
              touch(vec[i1]);
            }
          }
        }
      }
    }
  }
}

void test_nested_loop_7(float* vec, int n) {
  _Cilk_for (int i7 = 0; i7 < n; ++i7) {
    touch(vec[i7]);
    _Cilk_for (int i6 = 0; i6 < n; ++i6) {
      touch(vec[i6]);
      _Cilk_for (int i5 = 0; i5 < n; ++i5) {
        touch(vec[i5]);
        _Cilk_for (int i4 = 0; i4 < n; ++i4) {
          touch(vec[i4]);
          _Cilk_for (int i3 = 0; i3 < n; ++i3) {
            touch(vec[i3]);
            _Cilk_for (int i2 = 0; i2 < n; ++i2) {
              touch(vec[i2]);
              _Cilk_for (int i1 = 0; i1 < n; ++i1) {
                touch(vec[i1]);
              }
            }
          }
        }
      }
    }
  }
}

void test_nested_loop_8(float* vec, int n) {
  _Cilk_for (int i8 = 0; i8 < n; ++i8) {
    touch(vec[i8]);
    _Cilk_for (int i7 = 0; i7 < n; ++i7) {
      touch(vec[i7]);
      _Cilk_for (int i6 = 0; i6 < n; ++i6) {
        touch(vec[i6]);
        _Cilk_for (int i5 = 0; i5 < n; ++i5) {
          touch(vec[i5]);
          _Cilk_for (int i4 = 0; i4 < n; ++i4) {
            touch(vec[i4]);
            _Cilk_for (int i3 = 0; i3 < n; ++i3) {
              touch(vec[i3]);
              _Cilk_for (int i2 = 0; i2 < n; ++i2) {
                touch(vec[i2]);
                _Cilk_for (int i1 = 0; i1 < n; ++i1) {
                  touch(vec[i1]);
                }
              }
            }
          }
        }
      }
    }
  }
}

void test_nested_loop_9(float* vec, int n) {
  _Cilk_for (int i9 = 0; i9 < n; ++i9) {
    touch(vec[i9]);
    _Cilk_for (int i8 = 0; i8 < n; ++i8) {
      touch(vec[i8]);
      _Cilk_for (int i7 = 0; i7 < n; ++i7) {
        touch(vec[i7]);
        _Cilk_for (int i6 = 0; i6 < n; ++i6) {
          touch(vec[i6]);
          _Cilk_for (int i5 = 0; i5 < n; ++i5) {
            touch(vec[i5]);
            _Cilk_for (int i4 = 0; i4 < n; ++i4) {
              touch(vec[i4]);
              _Cilk_for (int i3 = 0; i3 < n; ++i3) {
                touch(vec[i3]);
                _Cilk_for (int i2 = 0; i2 < n; ++i2) {
                  touch(vec[i2]);
                  _Cilk_for (int i1 = 0; i1 < n; ++i1) {
                    touch(vec[i1]);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void test_nested_loop_10(float* vec, int n) {
  _Cilk_for (int i10 = 0; i10 < n; ++i10) {
    touch(vec[i10]);
    _Cilk_for (int i9 = 0; i9 < n; ++i9) {
      touch(vec[i9]);
      _Cilk_for (int i8 = 0; i8 < n; ++i8) {
        touch(vec[i8]);
        _Cilk_for (int i7 = 0; i7 < n; ++i7) {
          touch(vec[i7]);
          _Cilk_for (int i6 = 0; i6 < n; ++i6) {
            touch(vec[i6]);
            _Cilk_for (int i5 = 0; i5 < n; ++i5) {
              touch(vec[i5]);
              _Cilk_for (int i4 = 0; i4 < n; ++i4) {
                touch(vec[i4]);
                _Cilk_for (int i3 = 0; i3 < n; ++i3) {
                  touch(vec[i3]);
                  _Cilk_for (int i2 = 0; i2 < n; ++i2) {
                    touch(vec[i2]);
                    _Cilk_for (int i1 = 0; i1 < n; ++i1) {
                      touch(vec[i1]);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void test_nested_loop_15(float* vec, int n) {
  _Cilk_for (int i15 = 0; i15 < n; ++i15) {
    touch(vec[i15]);
    _Cilk_for (int i14 = 0; i14 < n; ++i14) {
      touch(vec[i14]);
      _Cilk_for (int i13 = 0; i13 < n; ++i13) {
        touch(vec[i13]);
        _Cilk_for (int i12 = 0; i12 < n; ++i12) {
          touch(vec[i12]);
          _Cilk_for (int i11 = 0; i11 < n; ++i11) {
            touch(vec[i11]);
            _Cilk_for (int i10 = 0; i10 < n; ++i10) {
              touch(vec[i10]);
              _Cilk_for (int i9 = 0; i9 < n; ++i9) {
                touch(vec[i9]);
                _Cilk_for (int i8 = 0; i8 < n; ++i8) {
                  touch(vec[i8]);
                  _Cilk_for (int i7 = 0; i7 < n; ++i7) {
                    touch(vec[i7]);
                    _Cilk_for (int i6 = 0; i6 < n; ++i6) {
                      touch(vec[i6]);
                      _Cilk_for (int i5 = 0; i5 < n; ++i5) {
                        touch(vec[i5]);
                        _Cilk_for (int i4 = 0; i4 < n; ++i4) {
                          touch(vec[i4]);
                          _Cilk_for (int i3 = 0; i3 < n; ++i3) {
                            touch(vec[i3]);
                            _Cilk_for (int i2 = 0; i2 < n; ++i2) {
                              touch(vec[i2]);
                              _Cilk_for (int i1 = 0; i1 < n; ++i1) {
                                touch(vec[i1]);
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void test_nested_loop_20(float* vec, int n) {
  _Cilk_for (int i20 = 0; i20 < n; ++i20) {
    touch(vec[i20]);
    _Cilk_for (int i19 = 0; i19 < n; ++i19) {
      touch(vec[i19]);
      _Cilk_for (int i18 = 0; i18 < n; ++i18) {
        touch(vec[i18]);
        _Cilk_for (int i17 = 0; i17 < n; ++i17) {
          touch(vec[i17]);
          _Cilk_for (int i16 = 0; i16 < n; ++i16) {
            touch(vec[i16]);
            _Cilk_for (int i15 = 0; i15 < n; ++i15) {
              touch(vec[i15]);
              _Cilk_for (int i14 = 0; i14 < n; ++i14) {
                touch(vec[i14]);
                _Cilk_for (int i13 = 0; i13 < n; ++i13) {
                  touch(vec[i13]);
                  _Cilk_for (int i12 = 0; i12 < n; ++i12) {
                    touch(vec[i12]);
                    _Cilk_for (int i11 = 0; i11 < n; ++i11) {
                      touch(vec[i11]);
                      _Cilk_for (int i10 = 0; i10 < n; ++i10) {
                        touch(vec[i10]);
                        _Cilk_for (int i9 = 0; i9 < n; ++i9) {
                          touch(vec[i9]);
                          _Cilk_for (int i8 = 0; i8 < n; ++i8) {
                            touch(vec[i8]);
                            _Cilk_for (int i7 = 0; i7 < n; ++i7) {
                              touch(vec[i7]);
                              _Cilk_for (int i6 = 0; i6 < n; ++i6) {
                                touch(vec[i6]);
                                _Cilk_for (int i5 = 0; i5 < n; ++i5) {
                                  touch(vec[i5]);
                                  _Cilk_for (int i4 = 0; i4 < n; ++i4) {
                                    touch(vec[i4]);
                                    _Cilk_for (int i3 = 0; i3 < n; ++i3) {
                                      touch(vec[i3]);
                                      _Cilk_for (int i2 = 0; i2 < n; ++i2) {
                                        touch(vec[i2]);
                                        _Cilk_for (int i1 = 0; i1 < n; ++i1) {
                                          touch(vec[i1]);
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

#define cilk_for _Cilk_for
#define cilk_spawn _Cilk_spawn
extern void f0(float *x);
int test999()
{
  float vec1[10];
  cilk_for (int i00 = 0; i00 < 2; i00++)
    cilk_for (int i01 = 0; i01 <= i00; i01++)
      cilk_spawn f0(vec1 + (i00|i00));
  return 0;
}

// CHECK: define {{.+}}test999
// CHECK: call {{.+}}__cilkrts_cilk_for
// CHECK: define {{.+}}__cilk_for_helper
// CHECK: define {{.+}}__cilk_for_helper


int test8888(){
  int i, j;
  _Cilk_for (i = 0; i < 8; i++)
    _Cilk_for (j = 0; j < 50; j++)
      {
        //do nothing
      }
  return 0; 
}

// CHECK: define {{.+}}test8888
// CHECK: call {{.+}}__cilkrts_cilk_for
// CHECK: call {{.+}}__cilkrts_cilk_for
