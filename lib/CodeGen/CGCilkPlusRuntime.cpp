//===----- CGCilkPlusRuntime.cpp - Interface to the Cilk Plus Runtime -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// \brief This files implements Cilk Plus code generation. The purpose of the
/// runtime is to encapsulate everything for Cilk spawn/sync/for. This includes
/// making calls to the cilkrts library and call to the spawn helper function.
///
//===----------------------------------------------------------------------===//
#include "CGCilkPlusRuntime.h"
#include "CGCleanup.h"
#include "CodeGenFunction.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

namespace {

typedef void *__CILK_JUMP_BUFFER[5];

struct __cilkrts_pedigree {};
struct __cilkrts_stack_frame {};
struct __cilkrts_worker {};

enum {
  __CILKRTS_ABI_VERSION = 1
};

enum {
  CILK_FRAME_STOLEN           =    0x01,
  CILK_FRAME_UNSYNCHED        =    0x02,
  CILK_FRAME_DETACHED         =    0x04,
  CILK_FRAME_EXCEPTION_PROBED =    0x08,
  CILK_FRAME_EXCEPTING        =    0x10,
  CILK_FRAME_LAST             =    0x80,
  CILK_FRAME_EXITING          =  0x0100,
  CILK_FRAME_SUSPENDED        =  0x8000,
  CILK_FRAME_UNWINDING        = 0x10000
};

#define CILK_FRAME_VERSION (__CILKRTS_ABI_VERSION << 24)
#define CILK_FRAME_VERSION_MASK  0xFF000000
#define CILK_FRAME_FLAGS_MASK    0x00FFFFFF
#define CILK_FRAME_VERSION_VALUE(_flags) (((_flags) & CILK_FRAME_VERSION_MASK) >> 24)
#define CILK_FRAME_MBZ  (~ (CILK_FRAME_STOLEN           | \
                            CILK_FRAME_UNSYNCHED        | \
                            CILK_FRAME_DETACHED         | \
                            CILK_FRAME_EXCEPTION_PROBED | \
                            CILK_FRAME_EXCEPTING        | \
                            CILK_FRAME_LAST             | \
                            CILK_FRAME_EXITING          | \
                            CILK_FRAME_SUSPENDED        | \
                            CILK_FRAME_UNWINDING        | \
                            CILK_FRAME_VERSION_MASK))

typedef uint32_t cilk32_t;
typedef uint64_t cilk64_t;
typedef void (*__cilk_abi_f32_t)(void *data, cilk32_t low, cilk32_t high);
typedef void (*__cilk_abi_f64_t)(void *data, cilk64_t low, cilk64_t high);

typedef void (__cilkrts_enter_frame)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_enter_frame_1)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_enter_frame_fast)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_enter_frame_fast_1)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_leave_frame)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_sync)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_return_exception)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_rethrow)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_detach)(__cilkrts_stack_frame *sf);
typedef void (__cilkrts_pop_frame)(__cilkrts_stack_frame *sf);
typedef __cilkrts_worker *(__cilkrts_get_tls_worker)();
typedef __cilkrts_worker *(__cilkrts_get_tls_worker_fast)();
typedef __cilkrts_worker *(__cilkrts_bind_thread_1)();
typedef void (__cilkrts_cilk_for_32)(__cilk_abi_f32_t body, void *data,
                                     cilk32_t count, int grain);
typedef void (__cilkrts_cilk_for_64)(__cilk_abi_f64_t body, void *data,
                                     cilk64_t count, int grain);

typedef void (cilk_func)(__cilkrts_stack_frame *);

} // namespace

#define CILKRTS_FUNC(name, CGF) Get__cilkrts_##name(CGF)

#define DEFAULT_GET_CILKRTS_FUNC(name) \
static llvm::Function *Get__cilkrts_##name(clang::CodeGen::CodeGenFunction &CGF) { \
   return llvm::cast<llvm::Function>(CGF.CGM.CreateRuntimeFunction( \
      llvm::TypeBuilder<__cilkrts_##name, false>::get(CGF.getLLVMContext()), \
      "__cilkrts_"#name)); \
}

DEFAULT_GET_CILKRTS_FUNC(sync)
DEFAULT_GET_CILKRTS_FUNC(rethrow)
DEFAULT_GET_CILKRTS_FUNC(leave_frame)
DEFAULT_GET_CILKRTS_FUNC(get_tls_worker)
DEFAULT_GET_CILKRTS_FUNC(bind_thread_1)

typedef std::map<llvm::LLVMContext*, llvm::StructType*> TypeBuilderCache;

namespace llvm {

/// Specializations of llvm::TypeBuilder for:
///   __cilkrts_pedigree,
///   __cilkrts_worker,
///   __cilkrts_stack_frame
template <bool X>
class TypeBuilder<__cilkrts_pedigree, X> {
public:
  static StructType *get(LLVMContext &C) {
    static TypeBuilderCache cache;
    TypeBuilderCache::iterator I = cache.find(&C);
    if (I != cache.end())
      return I->second;
    StructType *Ty = StructType::create(C, "__cilkrts_pedigree");
    cache[&C] = Ty;
    Ty->setBody(
      TypeBuilder<uint64_t,            X>::get(C), // rank
      TypeBuilder<__cilkrts_pedigree*, X>::get(C), // next
      NULL);
    return Ty;
  }
  enum {
    rank,
    next
  };
};

template <bool X>
class TypeBuilder<__cilkrts_worker, X> {
public:
  static StructType *get(LLVMContext &C) {
    static TypeBuilderCache cache;
    TypeBuilderCache::iterator I = cache.find(&C);
    if (I != cache.end())
      return I->second;
    StructType *Ty = StructType::create(C, "__cilkrts_worker");
    cache[&C] = Ty;
    Ty->setBody(
      TypeBuilder<__cilkrts_stack_frame**, X>::get(C), // tail
      TypeBuilder<__cilkrts_stack_frame**, X>::get(C), // head
      TypeBuilder<__cilkrts_stack_frame**, X>::get(C), // exc
      TypeBuilder<__cilkrts_stack_frame**, X>::get(C), // protected_tail
      TypeBuilder<__cilkrts_stack_frame**, X>::get(C), // ltq_limit
      TypeBuilder<int32_t,                 X>::get(C), // self
      TypeBuilder<void*,                   X>::get(C), // g
      TypeBuilder<void*,                   X>::get(C), // l
      TypeBuilder<void*,                   X>::get(C), // reducer_map
      TypeBuilder<__cilkrts_stack_frame*,  X>::get(C), // current_stack_frame
      TypeBuilder<__cilkrts_stack_frame**, X>::get(C), // saved_protected_tail
      TypeBuilder<void*,                   X>::get(C), // sysdep
      TypeBuilder<__cilkrts_pedigree,      X>::get(C), // pedigree
      NULL);
    return Ty;
  }
  enum {
    tail,
    head,
    exc,
    protected_tail,
    ltq_limit,
    self,
    g,
    l,
    reducer_map,
    current_stack_frame,
    saved_protected_tail,
    sysdep,
    pedigree
  };
};

template <bool X>
class TypeBuilder<__cilkrts_stack_frame, X> {
public:
  static StructType *get(LLVMContext &C) {
    static TypeBuilderCache cache;
    TypeBuilderCache::iterator I = cache.find(&C);
    if (I != cache.end())
      return I->second;
    StructType *Ty = StructType::create(C, "__cilkrts_stack_frame");
    cache[&C] = Ty;
    Ty->setBody(
      TypeBuilder<uint32_t,               X>::get(C), // flags
      TypeBuilder<int32_t,                X>::get(C), // size
      TypeBuilder<__cilkrts_stack_frame*, X>::get(C), // call_parent
      TypeBuilder<__cilkrts_worker*,      X>::get(C), // worker
      TypeBuilder<void*,                  X>::get(C), // except_data
      TypeBuilder<__CILK_JUMP_BUFFER,     X>::get(C), // ctx
      TypeBuilder<uint32_t,               X>::get(C), // mxcsr
      TypeBuilder<uint16_t,               X>::get(C), // fpcsr
      TypeBuilder<uint16_t,               X>::get(C), // reserved
      TypeBuilder<__cilkrts_pedigree,     X>::get(C), // parent_pedigree
      NULL);
    return Ty;
  }
  enum {
    flags,
    size,
    call_parent,
    worker,
    except_data,
    ctx,
    mxcsr,
    fpcsr,
    reserved,
    parent_pedigree
  };
};

} // namespace llvm

using namespace clang;
using namespace CodeGen;
using namespace llvm;

/// Helper typedefs for cilk struct TypeBuilders.
typedef llvm::TypeBuilder<__cilkrts_stack_frame, false> StackFrameBuilder;
typedef llvm::TypeBuilder<__cilkrts_worker, false> WorkerBuilder;
typedef llvm::TypeBuilder<__cilkrts_pedigree, false> PedigreeBuilder;

static Value *GEP(CGBuilderTy &B, Value *Base, int field) {
  return B.CreateConstInBoundsGEP2_32(Base, 0, field);
}

static void StoreField(CGBuilderTy &B, Value *Val, Value *Dst, int field) {
  B.CreateStore(Val, GEP(B, Dst, field));
}

static Value *LoadField(CGBuilderTy &B, Value *Src, int field) {
  return B.CreateLoad(GEP(B, Src, field));
}

/// \brief Emit inline assembly code to save the floating point
/// state, for x86 Only.
static void EmitSaveFloatingPointState(CGBuilderTy &B, Value *SF) {
  typedef void (AsmPrototype)(uint32_t*, uint16_t*);
  llvm::FunctionType *FTy =
    TypeBuilder<AsmPrototype, false>::get(B.getContext());

  Value *Asm = InlineAsm::get(FTy,
                              "stmxcsr $0\n\t" "fnstcw $1",
                              "*m,*m,~{dirflag},~{fpsr},~{flags}",
                              /*sideeffects*/ true);

  Value *mxcsrField = GEP(B, SF, StackFrameBuilder::mxcsr);
  Value *fpcsrField = GEP(B, SF, StackFrameBuilder::fpcsr);

  B.CreateCall2(Asm, mxcsrField, fpcsrField);
}

/// \brief Helper to find a function with the given name, creating it if it
/// doesn't already exist. If the function needed to be created then return
/// false, signifying that the caller needs to add the function body.
template <typename T>
static bool GetOrCreateFunction(const char *FnName, CodeGenFunction& CGF,
                                Function *&Fn, Function::LinkageTypes Linkage =
                                               Function::InternalLinkage,
                                bool DoesNotThrow = true) {
  llvm::Module &Module = CGF.CGM.getModule();
  LLVMContext &Ctx = CGF.getLLVMContext();

  Fn = Module.getFunction(FnName);

  // if the function already exists then let the
  // caller know that it is complete
  if (Fn)
    return true;

  // Otherwise we have to create it
  llvm::FunctionType *FTy = TypeBuilder<T, false>::get(Ctx);
  Fn = Function::Create(FTy, Linkage, FnName, &Module);

  // Set nounwind if it does not throw.
  if (DoesNotThrow)
    Fn->setDoesNotThrow();

  // and let the caller know that the function is incomplete
  // and the body still needs to be added
  return false;
}

/// \brief Register a sync function with a named metadata.
static void registerSyncFunction(CodeGenFunction &CGF, llvm::Function *Fn) {
  LLVMContext &Context = CGF.getLLVMContext();
  llvm::NamedMDNode *SyncMetadata
    = CGF.CGM.getModule().getOrInsertNamedMetadata("cilk.sync");

  SyncMetadata->addOperand(llvm::MDNode::get(Context, Fn));
}

/// \brief Register a spawn helper function with a named metadata.
static void registerSpawnFunction(CodeGenFunction &CGF, llvm::Function *Fn) {
  LLVMContext &Context = CGF.getLLVMContext();
  llvm::NamedMDNode *SpawnMetadata
    = CGF.CGM.getModule().getOrInsertNamedMetadata("cilk.spawn");

  SpawnMetadata->addOperand(llvm::MDNode::get(Context, Fn));
}

/// \brief Emit a call to the CILK_SETJMP function.
static CallInst *EmitCilkSetJmp(CGBuilderTy &B, Value *SF,
                                CodeGenFunction &CGF) {
  LLVMContext &Ctx = CGF.getLLVMContext();

  // We always want to save the floating point state too
  EmitSaveFloatingPointState(B, SF);

  llvm::Type *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  llvm::Type *Int8PtrTy = llvm::Type::getInt8PtrTy(Ctx);

  // Get the buffer to store program state
  // Buffer is a void**.
  Value *Buf = GEP(B, SF, StackFrameBuilder::ctx);

  // Store the frame pointer in the 0th slot
  Value *FrameAddr =
    B.CreateCall(CGF.CGM.getIntrinsic(Intrinsic::frameaddress),
                 ConstantInt::get(Int32Ty, 0));

  Value *FrameSaveSlot = GEP(B, Buf, 0);
  B.CreateStore(FrameAddr, FrameSaveSlot);

  // Store stack pointer in the 2nd slot
  Value *StackAddr = B.CreateCall(CGF.CGM.getIntrinsic(Intrinsic::stacksave));

  Value *StackSaveSlot = GEP(B, Buf, 2);
  B.CreateStore(StackAddr, StackSaveSlot);

  // Call LLVM's EH setjmp, which is lightweight.
  Value *F = CGF.CGM.getIntrinsic(Intrinsic::eh_sjlj_setjmp);
  Buf = B.CreateBitCast(Buf, Int8PtrTy);

  CallInst *SetjmpCall = B.CreateCall(F, Buf);
  SetjmpCall->setCanReturnTwice();
  return SetjmpCall;
}

/// \brief Get or create a LLVM function for __cilkrts_pop_frame.
/// It is equivalent to the following C code
///
/// __cilkrts_pop_frame(__cilkrts_stack_frame *sf) {
///   sf->worker->current_stack_frame = sf->call_parent;
///   sf->call_parent = 0;
/// }
static Function *Get__cilkrts_pop_frame(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilkrts_pop_frame", CGF, Fn))
    return Fn;

  // If we get here we need to add the function body
  LLVMContext &Ctx = CGF.getLLVMContext();

  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn);
  CGBuilderTy B(Entry);

  // sf->worker->current_stack_frame = sf.call_parent;
  StoreField(B,
    LoadField(B, SF, StackFrameBuilder::call_parent),
    LoadField(B, SF, StackFrameBuilder::worker),
    WorkerBuilder::current_stack_frame);

  // sf->call_parent = 0;
  StoreField(B,
    Constant::getNullValue(TypeBuilder<__cilkrts_stack_frame*, false>::get(Ctx)),
    SF, StackFrameBuilder::call_parent);

  B.CreateRetVoid();

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilkrts_detach.
/// It is equivalent to the following C code
///
/// void __cilkrts_detach(struct __cilkrts_stack_frame *sf) {
///   struct __cilkrts_worker *w = sf->worker;
///   struct __cilkrts_stack_frame *volatile *tail = w->tail;
///
///   sf->spawn_helper_pedigree = w->pedigree;
///   sf->call_parent->parent_pedigree = w->pedigree;
///
///   w->pedigree.rank = 0;
///   w->pedigree.next = &sf->spawn_helper_pedigree;
///
///   *tail++ = sf->call_parent;
///   w->tail = tail;
///
///   sf->flags |= CILK_FRAME_DETACHED;
/// }
static Function *Get__cilkrts_detach(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilkrts_detach", CGF, Fn))
    return Fn;

  // If we get here we need to add the function body
  LLVMContext &Ctx = CGF.getLLVMContext();

  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn);
  CGBuilderTy B(Entry);

  // struct __cilkrts_worker *w = sf->worker;
  Value *W = LoadField(B, SF, StackFrameBuilder::worker);

  // __cilkrts_stack_frame *volatile *tail = w->tail;
  Value *Tail = LoadField(B, W, WorkerBuilder::tail);

  // sf->spawn_helper_pedigree = w->pedigree;
  StoreField(B,
             LoadField(B, W, WorkerBuilder::pedigree),
             SF, StackFrameBuilder::parent_pedigree);

  // sf->call_parent->parent_pedigree = w->pedigree;
  StoreField(B,
             LoadField(B, W, WorkerBuilder::pedigree),
             LoadField(B, SF, StackFrameBuilder::call_parent),
             StackFrameBuilder::parent_pedigree);

  // w->pedigree.rank = 0;
  {
    StructType *STy = PedigreeBuilder::get(Ctx);
    llvm::Type *Ty = STy->getElementType(PedigreeBuilder::rank);
    StoreField(B,
               ConstantInt::get(Ty, 0),
               GEP(B, W, WorkerBuilder::pedigree),
               PedigreeBuilder::rank);
  }

  // w->pedigree.next = &sf->spawn_helper_pedigree;
  StoreField(B,
             GEP(B, SF, StackFrameBuilder::parent_pedigree),
             GEP(B, W, WorkerBuilder::pedigree),
             PedigreeBuilder::next);

  // *tail++ = sf->call_parent;
  B.CreateStore(LoadField(B, SF, StackFrameBuilder::call_parent), Tail);
  Tail = B.CreateConstGEP1_32(Tail, 1);

  // w->tail = tail;
  StoreField(B, Tail, W, WorkerBuilder::tail);

  // sf->flags |= CILK_FRAME_DETACHED;
  {
    Value *F = LoadField(B, SF, StackFrameBuilder::flags);
    F = B.CreateOr(F, ConstantInt::get(F->getType(), CILK_FRAME_DETACHED));
    StoreField(B, F, SF, StackFrameBuilder::flags);
  }

  B.CreateRetVoid();

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilk_excepting_sync.
/// This is a special sync to be inserted before processing a catch statement.
/// Calls to this function are always inlined.
///
/// It is equivalent to the following C code
///
/// void __cilk_excepting_sync(struct __cilkrts_stack_frame *sf, void **ExnSlot) {
///   if (sf->flags & CILK_FRAME_UNSYNCHED) {
///     if (!CILK_SETJMP(sf->ctx)) {
///       sf->except_data = *ExnSlot;
///       sf->flags |= CILK_FRAME_EXCEPTING;
///       __cilkrts_sync(sf);
///     }
///     sf->flags &= ~CILK_FRAME_EXCEPTING;
///     *ExnSlot = sf->except_data;
///   }
///   ++sf->worker->pedigree.rank;
/// }
static Function *GetCilkExceptingSyncFn(CodeGenFunction &CGF) {
  Function *Fn = 0;

  typedef void (cilk_func_1)(__cilkrts_stack_frame *, void **);
  if (GetOrCreateFunction<cilk_func_1>("__cilk_excepting_sync", CGF, Fn))
    return Fn;

  LLVMContext &Ctx = CGF.getLLVMContext();
  assert((Fn->arg_size() == 2) && "unexpected function type");
  Value *SF = Fn->arg_begin();
  Value *ExnSlot = ++Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn),
             *JumpTest = BasicBlock::Create(Ctx, "setjmp.test", Fn),
             *JumpIf = BasicBlock::Create(Ctx, "setjmp.if", Fn),
             *JumpCont = BasicBlock::Create(Ctx, "setjmp.cont", Fn),
             *Exit = BasicBlock::Create(Ctx, "exit", Fn);

  // Entry
  {
    CGBuilderTy B(Entry);

    // if (sf->flags & CILK_FRAME_UNSYNCHED)
    Value *Flags = LoadField(B, SF, StackFrameBuilder::flags);
    Flags = B.CreateAnd(Flags,
                        ConstantInt::get(Flags->getType(),
                                         CILK_FRAME_UNSYNCHED));
    Value *Zero = Constant::getNullValue(Flags->getType());

    Value *Unsynced = B.CreateICmpEQ(Flags, Zero);
    B.CreateCondBr(Unsynced, Exit, JumpTest);
  }

  // JumpTest
  {
    CGBuilderTy B(JumpTest);
    // if (!CILK_SETJMP(sf.ctx))
    Value *C = EmitCilkSetJmp(B, SF, CGF);
    C = B.CreateICmpEQ(C, Constant::getNullValue(C->getType()));
    B.CreateCondBr(C, JumpIf, JumpCont);
  }

  // JumpIf
  {
    CGBuilderTy B(JumpIf);

    // sf->except_data = *ExnSlot;
    StoreField(B, B.CreateLoad(ExnSlot), SF, StackFrameBuilder::except_data);

    // sf->flags |= CILK_FRAME_EXCEPTING;
    Value *Flags = LoadField(B, SF, StackFrameBuilder::flags);
    Flags = B.CreateOr(Flags, ConstantInt::get(Flags->getType(),
                                               CILK_FRAME_EXCEPTING));
    StoreField(B, Flags, SF, StackFrameBuilder::flags);

    // __cilkrts_sync(&sf);
    B.CreateCall(CILKRTS_FUNC(sync, CGF), SF);
    B.CreateBr(JumpCont);
  }

  // JumpCont
  {
    CGBuilderTy B(JumpCont);

    // sf->flags &= ~CILK_FRAME_EXCEPTING;
    Value *Flags = LoadField(B, SF, StackFrameBuilder::flags);
    Flags = B.CreateAnd(Flags, ConstantInt::get(Flags->getType(),
                                                ~CILK_FRAME_EXCEPTING));
    StoreField(B, Flags, SF, StackFrameBuilder::flags);

    // Exn = sf->except_data;
    B.CreateStore(LoadField(B, SF, StackFrameBuilder::except_data), ExnSlot);
    B.CreateBr(Exit);
  }

  // Exit
  {
    CGBuilderTy B(Exit);

    // ++sf.worker->pedigree.rank;
    Value *Rank = LoadField(B, SF, StackFrameBuilder::worker);
    Rank = GEP(B, Rank, WorkerBuilder::pedigree);
    Rank = GEP(B, Rank, PedigreeBuilder::rank);
    B.CreateStore(B.CreateAdd(B.CreateLoad(Rank),
                  ConstantInt::get(Rank->getType()->getPointerElementType(),
                                   1)),
                  Rank);
    B.CreateRetVoid();
  }

  Fn->addFnAttr(Attribute::AlwaysInline);
  Fn->addFnAttr(Attribute::ReturnsTwice);
//***INTEL
  // Special Intel-specific attribute for inliner.
  Fn->addFnAttr("INTEL_ALWAYS_INLINE");
  registerSyncFunction(CGF, Fn);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilk_sync.
/// Calls to this function is always inlined, as it saves
/// the current stack/frame pointer values. This function must be marked
/// as returns_twice to allow it to be inlined, since the call to setjmp
/// is marked returns_twice.
///
/// It is equivalent to the following C code
///
/// void __cilk_sync(struct __cilkrts_stack_frame *sf) {
///   if (sf->flags & CILK_FRAME_UNSYNCHED) {
///     sf->parent_pedigree = sf->worker->pedigree;
///     SAVE_FLOAT_STATE(*sf);
///     if (!CILK_SETJMP(sf->ctx))
///       __cilkrts_sync(sf);
///     else if (sf->flags & CILK_FRAME_EXCEPTING)
///       __cilkrts_rethrow(sf);
///   }
///   ++sf->worker->pedigree.rank;
/// }
///
/// With exceptions disabled in the compiler, the function
/// does not call __cilkrts_rethrow()
static Function *GetCilkSyncFn(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilk_sync", CGF, Fn,
                                     Function::InternalLinkage,
                                     /*doesNotThrow*/false))
    return Fn;

  // If we get here we need to add the function body
  LLVMContext &Ctx = CGF.getLLVMContext();

  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "cilk.sync.test", Fn),
             *SaveState = BasicBlock::Create(Ctx, "cilk.sync.savestate", Fn),
             *SyncCall = BasicBlock::Create(Ctx, "cilk.sync.runtimecall", Fn),
             *Excepting = BasicBlock::Create(Ctx, "cilk.sync.excepting", Fn),
             *Rethrow = CGF.CGM.getLangOpts().Exceptions ?
                          BasicBlock::Create(Ctx, "cilk.sync.rethrow", Fn) : 0,
             *Exit = BasicBlock::Create(Ctx, "cilk.sync.end", Fn);

  // Entry
  {
    CGBuilderTy B(Entry);

    // if (sf->flags & CILK_FRAME_UNSYNCHED)
    Value *Flags = LoadField(B, SF, StackFrameBuilder::flags);
    Flags = B.CreateAnd(Flags,
                        ConstantInt::get(Flags->getType(),
                                         CILK_FRAME_UNSYNCHED));
    Value *Zero = ConstantInt::get(Flags->getType(), 0);
    Value *Unsynced = B.CreateICmpEQ(Flags, Zero);
    B.CreateCondBr(Unsynced, Exit, SaveState);
  }

  // SaveState
  {
    CGBuilderTy B(SaveState);

    // sf.parent_pedigree = sf.worker->pedigree;
    StoreField(B,
      LoadField(B, LoadField(B, SF, StackFrameBuilder::worker),
                WorkerBuilder::pedigree),
      SF, StackFrameBuilder::parent_pedigree);

    // if (!CILK_SETJMP(sf.ctx))
    Value *C = EmitCilkSetJmp(B, SF, CGF);
    C = B.CreateICmpEQ(C, ConstantInt::get(C->getType(), 0));
    B.CreateCondBr(C, SyncCall, Excepting);
  }

  // SyncCall
  {
    CGBuilderTy B(SyncCall);

    // __cilkrts_sync(&sf);
    B.CreateCall(CILKRTS_FUNC(sync, CGF), SF);
    B.CreateBr(Exit);
  }

  // Excepting
  {
    CGBuilderTy B(Excepting);
    if (CGF.CGM.getLangOpts().Exceptions) {
      Value *Flags = LoadField(B, SF, StackFrameBuilder::flags);
      Flags = B.CreateAnd(Flags,
                          ConstantInt::get(Flags->getType(),
                                          CILK_FRAME_EXCEPTING));
      Value *Zero = ConstantInt::get(Flags->getType(), 0);
      Value *C = B.CreateICmpEQ(Flags, Zero);
      B.CreateCondBr(C, Exit, Rethrow);
    } else {
      B.CreateBr(Exit);
    }
  }

  // Rethrow
  if (CGF.CGM.getLangOpts().Exceptions) {
    CGBuilderTy B(Rethrow);
    B.CreateCall(CILKRTS_FUNC(rethrow, CGF), SF)->setDoesNotReturn();
    B.CreateUnreachable();
  }

  // Exit
  {
    CGBuilderTy B(Exit);

    // ++sf.worker->pedigree.rank;
    Value *Rank = LoadField(B, SF, StackFrameBuilder::worker);
    Rank = GEP(B, Rank, WorkerBuilder::pedigree);
    Rank = GEP(B, Rank, PedigreeBuilder::rank);
    B.CreateStore(B.CreateAdd(B.CreateLoad(Rank),
                  ConstantInt::get(Rank->getType()->getPointerElementType(),
                                   1)),
                  Rank);
    B.CreateRetVoid();
  }

  Fn->addFnAttr(Attribute::AlwaysInline);
  Fn->addFnAttr(Attribute::ReturnsTwice);
//***INTEL
  // Special Intel-specific attribute for inliner.
  Fn->addFnAttr("INTEL_ALWAYS_INLINE");
  registerSyncFunction(CGF, Fn);

  return Fn;
}

/// \brief Get or create a LLVM function to set worker to null value.
/// It is equivalent to the following C code
///
/// This is a utility function to ensure that __cilk_helper_epilogue
/// skips uninitialized stack frames.
///
/// void __cilk_reset_worker(__cilkrts_stack_frame *sf) {
///   sf->worker = 0;
/// }
///
static Function *GetCilkResetWorkerFn(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilk_reset_worker", CGF, Fn))
    return Fn;

  LLVMContext &Ctx = CGF.getLLVMContext();
  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn);
  CGBuilderTy B(Entry);

  // sf->worker = 0;
  StoreField(B,
    Constant::getNullValue(TypeBuilder<__cilkrts_worker*, false>::get(Ctx)),
    SF, StackFrameBuilder::worker);

  B.CreateRetVoid();

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilkrts_enter_frame.
/// It is equivalent to the following C code
///
/// void __cilkrts_enter_frame_1(struct __cilkrts_stack_frame *sf)
/// {
///     struct __cilkrts_worker *w = __cilkrts_get_tls_worker();
///     if (w == 0) { /* slow path, rare */
///         w = __cilkrts_bind_thread_1();
///         sf->flags = CILK_FRAME_LAST | CILK_FRAME_VERSION;
///     } else {
///         sf->flags = CILK_FRAME_VERSION;
///     }
///     sf->call_parent = w->current_stack_frame;
///     sf->worker = w;
///     /* sf->except_data is only valid when CILK_FRAME_EXCEPTING is set */
///     w->current_stack_frame = sf;
/// }
static Function *Get__cilkrts_enter_frame_1(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilkrts_enter_frame_1", CGF, Fn,
                                     Function::AvailableExternallyLinkage))
    return Fn;

  LLVMContext &Ctx = CGF.getLLVMContext();
  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "", Fn);
  BasicBlock *SlowPath = BasicBlock::Create(Ctx, "", Fn);
  BasicBlock *FastPath = BasicBlock::Create(Ctx, "", Fn);
  BasicBlock *Cont = BasicBlock::Create(Ctx, "", Fn);

  llvm::PointerType *WorkerPtrTy = TypeBuilder<__cilkrts_worker*, false>::get(Ctx);
  StructType *SFTy = StackFrameBuilder::get(Ctx);

  // Block  (Entry)
  CallInst *W = 0;
  {
    CGBuilderTy B(Entry);
    W = B.CreateCall(CILKRTS_FUNC(get_tls_worker, CGF));
    Value *Cond = B.CreateICmpEQ(W, ConstantPointerNull::get(WorkerPtrTy));
    B.CreateCondBr(Cond, SlowPath, FastPath);
  }
  // Block  (SlowPath)
  CallInst *Wslow = 0;
  {
    CGBuilderTy B(SlowPath);
    Wslow = B.CreateCall(CILKRTS_FUNC(bind_thread_1, CGF));
    llvm::Type *Ty = SFTy->getElementType(StackFrameBuilder::flags);
    StoreField(B,
      ConstantInt::get(Ty, CILK_FRAME_LAST | CILK_FRAME_VERSION),
      SF, StackFrameBuilder::flags);
    B.CreateBr(Cont);
  }
  // Block  (FastPath)
  {
    CGBuilderTy B(FastPath);
    llvm::Type *Ty = SFTy->getElementType(StackFrameBuilder::flags);
    StoreField(B,
      ConstantInt::get(Ty, CILK_FRAME_VERSION),
      SF, StackFrameBuilder::flags);
    B.CreateBr(Cont);
  }
  // Block  (Cont)
  {
    CGBuilderTy B(Cont);
    Value *Wfast = W;
    PHINode *W  = B.CreatePHI(WorkerPtrTy, 2);
    W->addIncoming(Wslow, SlowPath);
    W->addIncoming(Wfast, FastPath);

    StoreField(B,
      LoadField(B, W, WorkerBuilder::current_stack_frame),
      SF, StackFrameBuilder::call_parent);

    StoreField(B, W, SF, StackFrameBuilder::worker);
    StoreField(B, SF, W, WorkerBuilder::current_stack_frame);

    B.CreateRetVoid();
  }

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilkrts_enter_frame_fast.
/// It is equivalent to the following C code
///
/// void __cilkrts_enter_frame_fast_1(struct __cilkrts_stack_frame *sf)
/// {
///     struct __cilkrts_worker *w = __cilkrts_get_tls_worker();
///     sf->flags = CILK_FRAME_VERSION;
///     sf->call_parent = w->current_stack_frame;
///     sf->worker = w;
///     /* sf->except_data is only valid when CILK_FRAME_EXCEPTING is set */
///     w->current_stack_frame = sf;
/// }
static Function *Get__cilkrts_enter_frame_fast_1(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilkrts_enter_frame_fast_1", CGF, Fn,
                                     Function::AvailableExternallyLinkage))
    return Fn;

  LLVMContext &Ctx = CGF.getLLVMContext();
  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "", Fn);

  CGBuilderTy B(Entry);
  Value *W = B.CreateCall(CILKRTS_FUNC(get_tls_worker, CGF));
  StructType *SFTy = StackFrameBuilder::get(Ctx);
  llvm::Type *Ty = SFTy->getElementType(StackFrameBuilder::flags);

  StoreField(B,
    ConstantInt::get(Ty, CILK_FRAME_VERSION),
    SF, StackFrameBuilder::flags);
  StoreField(B,
    LoadField(B, W, WorkerBuilder::current_stack_frame),
    SF, StackFrameBuilder::call_parent);
  StoreField(B, W, SF, StackFrameBuilder::worker);
  StoreField(B, SF, W, WorkerBuilder::current_stack_frame);

  B.CreateRetVoid();

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}


/// \brief Get or create a LLVM function for __cilk_parent_prologue.
/// It is equivalent to the following C code
///
/// void __cilk_parent_prologue(__cilkrts_stack_frame *sf) {
///   __cilkrts_enter_frame_1(sf);
/// }
static Function *GetCilkParentPrologue(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilk_parent_prologue", CGF, Fn))
    return Fn;

  // If we get here we need to add the function body
  LLVMContext &Ctx = CGF.getLLVMContext();

  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn);
  CGBuilderTy B(Entry);

  // __cilkrts_enter_frame_1(sf)
  B.CreateCall(CILKRTS_FUNC(enter_frame_1, CGF), SF);

  B.CreateRetVoid();

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilk_parent_epilogue.
/// It is equivalent to the following C code
///
/// void __cilk_parent_epilogue(__cilkrts_stack_frame *sf) {
///   __cilkrts_pop_frame(sf);
///   if (sf->flags != CILK_FRAME_VERSION)
///     __cilkrts_leave_frame(sf);
/// }
static Function *GetCilkParentEpilogue(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilk_parent_epilogue", CGF, Fn))
    return Fn;

  // If we get here we need to add the function body
  LLVMContext &Ctx = CGF.getLLVMContext();

  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn),
             *B1 = BasicBlock::Create(Ctx, "", Fn),
             *Exit  = BasicBlock::Create(Ctx, "exit", Fn);

  // Entry
  {
    CGBuilderTy B(Entry);

    // __cilkrts_pop_frame(sf)
    B.CreateCall(CILKRTS_FUNC(pop_frame, CGF), SF);

    // if (sf->flags != CILK_FRAME_VERSION)
    Value *Flags = LoadField(B, SF, StackFrameBuilder::flags);
    Value *Cond = B.CreateICmpNE(Flags,
      ConstantInt::get(Flags->getType(), CILK_FRAME_VERSION));
    B.CreateCondBr(Cond, B1, Exit);
  }

  // B1
  {
    CGBuilderTy B(B1);

    // __cilkrts_leave_frame(sf);
    B.CreateCall(CILKRTS_FUNC(leave_frame, CGF), SF);
    B.CreateBr(Exit);
  }

  // Exit
  {
    CGBuilderTy B(Exit);
    B.CreateRetVoid();
  }

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilk_helper_prologue.
/// It is equivalent to the following C code
///
/// void __cilk_helper_prologue(__cilkrts_stack_frame *sf) {
///   __cilkrts_enter_frame_fast_1(sf);
///   __cilkrts_detach(sf);
/// }
static llvm::Function *GetCilkHelperPrologue(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilk_helper_prologue", CGF, Fn))
    return Fn;

  // If we get here we need to add the function body
  LLVMContext &Ctx = CGF.getLLVMContext();

  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn);
  CGBuilderTy B(Entry);

  // __cilkrts_enter_frame_fast_1(sf);
  B.CreateCall(CILKRTS_FUNC(enter_frame_fast_1, CGF), SF);

  // __cilkrts_detach(sf);
  B.CreateCall(CILKRTS_FUNC(detach, CGF), SF);

  B.CreateRetVoid();

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

/// \brief Get or create a LLVM function for __cilk_helper_epilogue.
/// It is equivalent to the following C code
///
/// void __cilk_helper_epilogue(__cilkrts_stack_frame *sf) {
///   if (sf->worker) {
///     __cilkrts_pop_frame(sf);
///     __cilkrts_leave_frame(sf);
///   }
/// }
static llvm::Function *GetCilkHelperEpilogue(CodeGenFunction &CGF) {
  Function *Fn = 0;

  if (GetOrCreateFunction<cilk_func>("__cilk_helper_epilogue", CGF, Fn))
    return Fn;

  // If we get here we need to add the function body
  LLVMContext &Ctx = CGF.getLLVMContext();

  Value *SF = Fn->arg_begin();

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Fn);
  BasicBlock *Body = BasicBlock::Create(Ctx, "body", Fn);
  BasicBlock *Exit = BasicBlock::Create(Ctx, "exit", Fn);

  // Entry
  {
    CGBuilderTy B(Entry);

    // if (sf->worker)
    Value *C = B.CreateIsNotNull(LoadField(B, SF, StackFrameBuilder::worker));
    B.CreateCondBr(C, Body, Exit);
  }

  // Body
  {
    CGBuilderTy B(Body);

    // __cilkrts_pop_frame(sf);
    B.CreateCall(CILKRTS_FUNC(pop_frame, CGF), SF);

    // __cilkrts_leave_frame(sf);
    B.CreateCall(CILKRTS_FUNC(leave_frame, CGF), SF);

    B.CreateBr(Exit);
  }

  // Exit
  {
    CGBuilderTy B(Exit);
    B.CreateRetVoid();
  }

  Fn->addFnAttr(Attribute::InlineHint);

  return Fn;
}

static const char *stack_frame_name = "__cilkrts_sf";

static llvm::Value *LookupStackFrame(CodeGenFunction &CGF) {
  return CGF.CurFn->getValueSymbolTable().lookup(stack_frame_name);
}

/// \brief Create the __cilkrts_stack_frame for the spawning function.
static llvm::Value *CreateStackFrame(CodeGenFunction &CGF) {
  assert(!LookupStackFrame(CGF) && "already created the stack frame");

  llvm::LLVMContext &Ctx = CGF.getLLVMContext();
  llvm::Type *SFTy = StackFrameBuilder::get(Ctx);
  llvm::AllocaInst *SF = CGF.CreateTempAlloca(SFTy);
  SF->setName(stack_frame_name);

  return SF;
}

namespace {
/// \brief Helper to find the spawn call.
///
class FindSpawnCallExpr : public RecursiveASTVisitor<FindSpawnCallExpr> {
public:
  const CallExpr *Spawn;

  explicit FindSpawnCallExpr(Stmt *Body) : Spawn(0) {
    TraverseStmt(Body);
  }

  bool VisitCallExpr(CallExpr *E) {
    if (E->isCilkSpawnCall()) {
      Spawn = E;
      return false; // exit
    }

    return true;
  }

  bool VisitCilkSpawnDecl(CilkSpawnDecl *D) {
    VisitStmt(D->getSpawnStmt());
    return false; // exit
  }

  bool TraverseLambdaExpr(LambdaExpr *) { return true; }
  bool TraverseBlockExpr(BlockExpr *) { return true; }
};

/// \brief Set attributes for the helper function.
///
/// The DoesNotThrow attribute should NOT be set during the semantic
/// analysis, since codegen will try to compute this attribute by
/// scanning the function body of the spawned function.
void setHelperAttributes(CodeGenFunction &CGF,
                         const Stmt *S,
                         Function *Helper) {
  FindSpawnCallExpr Finder(const_cast<Stmt *>(S));
  assert(Finder.Spawn && "spawn call expected");

  // Do not set for indirect spawn calls.
  if (const FunctionDecl *FD = Finder.Spawn->getDirectCallee()) {
    GlobalDecl GD(FD);
    llvm::Constant *Addr = CGF.CGM.GetAddrOfFunction(GD);

    // Strip off a bitcast if there is.
    if (llvm::ConstantExpr *CE = dyn_cast<llvm::ConstantExpr>(Addr)) {
      assert(CE->getOpcode() == llvm::Instruction::BitCast &&
             "function pointer bitcast expected");
      Addr = CE->getOperand(0);
    }

    Function *SpawnedFunc = dyn_cast<Function>(Addr);
    if (SpawnedFunc && SpawnedFunc->doesNotThrow())
      Helper->setDoesNotThrow();
  }

  // The helper function *cannot* be inlined.
  Helper->addFnAttr(llvm::Attribute::NoInline);
}

} // anonymous

namespace clang {
namespace CodeGen {

void CodeGenFunction::EmitCilkSpawnDecl(const CilkSpawnDecl *D) {
  // Get the __cilkrts_stack_frame
  Value *SF = LookupStackFrame(*this);
  assert(SF && "null stack frame unexpected");

  BasicBlock *Entry = createBasicBlock("cilk.spawn.savestate"),
             *Body  = createBasicBlock("cilk.spawn.helpercall"),
             *Exit  = createBasicBlock("cilk.spawn.continuation");

  EmitBlock(Entry);
  {
    CGBuilderTy B(Entry);

    // Need to save state before spawning
    Value *C = EmitCilkSetJmp(B, SF, *this);
    C = B.CreateICmpEQ(C, ConstantInt::get(C->getType(), 0));
    B.CreateCondBr(C, Body, Exit);
  }

  EmitBlock(Body);
  {
    // If this spawn initializes a variable, alloc this variable and
    // set it as the current receiver.
    VarDecl *VD = D->getReceiverDecl();
    if (VD) {
      switch (VD->getStorageClass()) {
      case SC_None:
      case SC_Auto:
      case SC_Register:
        EmitCaptureReceiverDecl(*VD);
        break;
      default:
        CGM.ErrorUnsupported(VD, "unexpected stroage class for a receiver");
      }
    }

    // Emit call to the helper function
    Function *Helper = EmitSpawnCapturedStmt(*D->getCapturedStmt(), VD);

    // Register the spawn helper function.
    registerSpawnFunction(*this, Helper);

    // Set other attributes.
    setHelperAttributes(*this, D->getSpawnStmt(), Helper);
  }
  EmitBlock(Exit);
}

void CodeGenFunction::EmitCilkSpawnExpr(const CilkSpawnExpr *E) {
  EmitCilkSpawnDecl(E->getSpawnDecl());
}

static void maybeCleanupBoundTemporary(CodeGenFunction &CGF,
                                       llvm::Value *ReceiverTmp,
                                       QualType InitTy) {
  const RecordType *RT = InitTy->getBaseElementTypeUnsafe()->getAs<RecordType>();
  if (!RT)
    return;

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(RT->getDecl());
  if (ClassDecl->hasTrivialDestructor())
    return;

  // If required, push a cleanup to destroy the temporary.
  const CXXDestructorDecl *Dtor = ClassDecl->getDestructor();
  if (InitTy->isArrayType())
    CGF.pushDestroy(NormalAndEHCleanup, ReceiverTmp,
                    InitTy, &CodeGenFunction::destroyCXXObject,
                    CGF.getLangOpts().Exceptions);
  else
    CGF.PushDestructorCleanup(Dtor, ReceiverTmp);
}

/// Generate an outlined function for the body of a CapturedStmt, store any
/// captured variables into the captured struct, and call the outlined function.
llvm::Function *
CodeGenFunction::EmitSpawnCapturedStmt(const CapturedStmt &S,
                                       VarDecl *ReceiverDecl) {
  const CapturedDecl *CD = S.getCapturedDecl();
  const RecordDecl *RD = S.getCapturedRecordDecl();
  assert(CD->hasBody() && "missing CapturedDecl body");

  LValue CapStruct = InitCapturedStruct(S);
  SmallVector<Value *, 3> Args;
  Args.push_back(CapStruct.getAddress());

  QualType ReceiverTmpType;
  llvm::Value *ReceiverTmp = 0;
  if (ReceiverDecl) {
    assert(CD->getNumParams() >= 2 && "receiver parameter expected");
    Args.push_back(GetAddrOfLocalVar(ReceiverDecl));
    if (CD->getNumParams() == 3) {
      ReceiverTmpType = CD->getParam(2)->getType()->getPointeeType();
      ReceiverTmp = CreateMemTemp(ReceiverTmpType);
      Args.push_back(ReceiverTmp);
    }
  }

  // Emit the CapturedDecl
  CodeGenFunction CGF(CGM, true);
  CGF.CapturedStmtInfo = new CGCilkSpawnInfo(S, ReceiverDecl);
  llvm::Function *F = CGF.GenerateCapturedStmtFunction(CD, RD, S.getLocStart());
  delete CGF.CapturedStmtInfo;

  // Emit call to the helper function.
  EmitCallOrInvoke(F, Args);

  // If this statement binds a temporary to a reference, then destroy the
  // temporary at the end of the reference's lifetime.
  if (ReceiverTmp)
    maybeCleanupBoundTemporary(*this, ReceiverTmp, ReceiverTmpType);

  return F;
}


/// \brief Emit a call to the __cilk_sync function.
void CGCilkPlusRuntime::EmitCilkSync(CodeGenFunction &CGF) {
  // Elide the sync if there is no stack frame initialized for this function.
  // This will happen if function only contains _Cilk_sync but no _Cilk_spawn.
  if (llvm::Value *SF = LookupStackFrame(CGF))
    CGF.EmitCallOrInvoke(GetCilkSyncFn(CGF), SF);
}

namespace {
/// \brief Cleanup for a spawn helper stack frame.
/// #if exception
///   sf.flags = sf.flags | CILK_FRAME_EXCEPTING;
///   sf.except_data = Exn;
/// #endif
///   __cilk_helper_epilogue(sf);
class SpawnHelperStackFrameCleanup : public EHScopeStack::Cleanup {
  llvm::Value *SF;
public:
  SpawnHelperStackFrameCleanup(llvm::Value *SF) : SF(SF) { }
  void Emit(CodeGenFunction &CGF, Flags F) {
    if (F.isForEHCleanup()) {
      llvm::Value *Exn = CGF.getExceptionFromSlot();

      // sf->flags |=CILK_FRAME_EXCEPTING;
      llvm::Value *Flags = LoadField(CGF.Builder, SF, StackFrameBuilder::flags);
      Flags = CGF.Builder.CreateOr(Flags, ConstantInt::get(Flags->getType(),
                                                           CILK_FRAME_EXCEPTING));
      StoreField(CGF.Builder, Flags, SF, StackFrameBuilder::flags);
      // sf->except_data = Exn;
      StoreField(CGF.Builder, Exn, SF, StackFrameBuilder::except_data);
    }

    // __cilk_helper_epilogue(sf);
    CGF.Builder.CreateCall(GetCilkHelperEpilogue(CGF), SF);
  }
};

/// \brief Cleanup for a spawn parent stack frame.
///   __cilk_parent_epilogue(sf);
class SpawnParentStackFrameCleanup : public EHScopeStack::Cleanup {
  llvm::Value *SF;
public:
  SpawnParentStackFrameCleanup(llvm::Value *SF) : SF(SF) { }
  void Emit(CodeGenFunction &CGF, Flags F) {
    CGF.Builder.CreateCall(GetCilkParentEpilogue(CGF), SF);
  }
};

/// \brief Cleanup to ensure parent stack frame is synced.
struct ImplicitSyncCleanup : public EHScopeStack::Cleanup {
  llvm::Value *SF;
public:
  ImplicitSyncCleanup(llvm::Value *SF) : SF(SF) { }
  void Emit(CodeGenFunction &CGF, Flags F) {
    if (F.isForEHCleanup()) {
      llvm::Value *ExnSlot = CGF.getExceptionSlot();
      assert(ExnSlot && "null exception handler slot");
      CGF.Builder.CreateCall2(GetCilkExceptingSyncFn(CGF), SF, ExnSlot);
    } else
      CGF.EmitCallOrInvoke(GetCilkSyncFn(CGF), SF);
  }
};

} // anonymous namespace

/// \brief Emit code to create a Cilk stack frame for the parent function and
/// release it in the end. This function should be only called once prior to
/// processing function parameters.
void CGCilkPlusRuntime::EmitCilkParentStackFrame(CodeGenFunction &CGF) {
  llvm::Value *SF = CreateStackFrame(CGF);

  // Need to initialize it by adding the prologue
  // to the top of the spawning function
  {
    assert(CGF.AllocaInsertPt && "not initializied");
    CGBuilderTy Builder(CGF.AllocaInsertPt);
    Builder.CreateCall(GetCilkParentPrologue(CGF), SF);
  }

  // Push cleanups associated to this stack frame initialization.
  CGF.EHStack.pushCleanup<SpawnParentStackFrameCleanup>(NormalAndEHCleanup, SF);
}

/// \brief Emit code to create a Cilk stack frame for the helper function and
/// release it in the end.
void CGCilkPlusRuntime::EmitCilkHelperStackFrame(CodeGenFunction &CGF) {
  llvm::Value *SF = CreateStackFrame(CGF);

  // Initialize the worker to null. If this worker is still null on exit,
  // then there is no stack frame constructed for spawning and there is no need
  // to cleanup this stack frame.
  CGF.Builder.CreateCall(GetCilkResetWorkerFn(CGF), SF);

  // Push cleanups associated to this stack frame initialization.
  CGF.EHStack.pushCleanup<SpawnHelperStackFrameCleanup>(NormalAndEHCleanup, SF);
}

/// \brief Push an implicit sync to the EHStack. A call to __cilk_sync will be
/// emitted on exit.
void CGCilkPlusRuntime::pushCilkImplicitSyncCleanup(CodeGenFunction &CGF) {
  // Get the __cilkrts_stack_frame
  Value *SF = LookupStackFrame(CGF);
  assert(SF && "null stack frame unexpected");

  CGF.EHStack.pushCleanup<ImplicitSyncCleanup>(NormalAndEHCleanup, SF);
}

/// \brief Emit necessary cilk runtime calls prior to call the spawned function.
/// This include the initialization of the helper stack frame and the detach.
void CGCilkPlusRuntime::EmitCilkHelperPrologue(CodeGenFunction &CGF) {
  // Get the __cilkrts_stack_frame
  Value *SF = LookupStackFrame(CGF);
  assert(SF && "null stack frame unexpected");

  // Initialize the stack frame and detach
  CGF.Builder.CreateCall(GetCilkHelperPrologue(CGF), SF);
}

/// \brief A utility function for finding the enclosing CXXTryStmt if exists.
/// If this statement is inside a CXXCatchStmt, then its enclosing CXXTryStmt is
/// not its parent. E.g.
/// \code
/// try {  // try-outer
///   try {   // try-inner
///     _Cilk_spawn f1();
///   } catch (...) {
///     _Cilk_spawn f2();
///   }
/// } catch (...) {
/// }
/// \endcode
/// Then spawn 'f1()' finds try-inner, but the spawn 'f2()' will find try-outer.
///
static CXXTryStmt *getEnclosingTryBlock(Stmt *S, const Stmt *Top,
                                        const ParentMap &PMap) {
  assert(S && "NULL Statement");

  while (true) {
    S = PMap.getParent(S);
    if (!S || S == Top)
      return 0;

    if (isa<CXXTryStmt>(S))
      return cast<CXXTryStmt>(S);

    if (isa<CXXCatchStmt>(S)) {
      Stmt *P = PMap.getParent(S);
      assert(isa<CXXTryStmt>(P) && "CXXTryStmt expected");
      // Skipping its enclosing CXXTryStmt
      S = PMap.getParent(P);
    }
  }

  return 0;
}

namespace {
/// \brief Helper class to determine
///
/// - if a try block needs an implicit sync on exit,
/// - if a spawning function needs an implicity sync on exit.
///
class TryStmtAnalyzer: public RecursiveASTVisitor<TryStmtAnalyzer> {
  /// \brief The function body to be analyzed.
  ///
  Stmt *Body;

  /// \brief A data structure to query the enclosing try-block.
  ///
  ParentMap &PMap;

  /// \brief A set of CXXTryStmt which needs an implicit sync on exit.
  ///
  CGCilkImplicitSyncInfo::SyncStmtSetTy &TrySet;

  /// \brief true if this spawning function needs an implicit sync.
  ///
  bool NeedsSync;

public:
  TryStmtAnalyzer(Stmt *Body, ParentMap &PMap,
                  CGCilkImplicitSyncInfo::SyncStmtSetTy &SyncSet)
    : Body(Body), PMap(PMap), TrySet(SyncSet), NeedsSync(false) {
    // Traverse the function body to collect all CXXTryStmt's which needs
    // an implicit on exit.
    TraverseStmt(Body);
  }

  bool TraverseLambdaExpr(LambdaExpr *E) { return true; }
  bool TraverseBlockExpr(BlockExpr *E) { return true; }
  bool TraverseCapturedStmt(CapturedStmt *) { return true; }
  bool VisitCilkSpawnExpr(CilkSpawnExpr *E) {
    CXXTryStmt *TS = getEnclosingTryBlock(E, Body, PMap);

    // If a spawn expr is not enclosed by any try-block, then
    // this function needs an implicit sync; otherwise, this try-block
    // needs an implicit sync.
    if (!TS)
      NeedsSync = true;
    else
      TrySet.insert(TS);

    return true;
  }

  bool VisitDeclStmt(DeclStmt *DS) {
    bool HasSpawn = false;
    for (DeclStmt::decl_iterator I = DS->decl_begin(), E = DS->decl_end();
        I != E; ++I) {
      if (isa<CilkSpawnDecl>(*I)) {
        HasSpawn = true;
        break;
      }
    }

    if (HasSpawn) {
      CXXTryStmt *TS = getEnclosingTryBlock(DS, Body, PMap);

      // If a spawn expr is not enclosed by any try-block, then
      // this function needs an implicit sync; otherwise, this try-block
      // needs an implicit sync.
      if (!TS)
        NeedsSync = true;
      else
        TrySet.insert(TS);
    }

    return true;
  }

  bool needsImplicitSync() const { return NeedsSync; }
};

/// \brief Helper class to determine if an implicit sync is needed for a
/// CXXThrowExpr.
class ThrowExprAnalyzer: public RecursiveASTVisitor<ThrowExprAnalyzer> {
  /// \brief The function body to be analyzed.
  ///
  Stmt *Body;

  /// \brief A data structure to query the enclosing try-block.
  ///
  ParentMap &PMap;

  /// \brief A set of CXXThrowExpr or CXXTryStmt's which needs an implicit
  /// sync before or on exit.
  ///
  CGCilkImplicitSyncInfo::SyncStmtSetTy &SyncSet;

  /// \brief true if this spawning function needs an implicit sync.
  ///
  const bool NeedsSync;

public:
  ThrowExprAnalyzer(Stmt *Body, ParentMap &PMap,
                    CGCilkImplicitSyncInfo::SyncStmtSetTy &SyncSet,
                    bool NeedsSync)
    : Body(Body), PMap(PMap), SyncSet(SyncSet), NeedsSync(NeedsSync) {
    TraverseStmt(Body);
  }

  bool TraverseLambdaExpr(LambdaExpr *E) { return true; }
  bool TraverseBlockExpr(BlockExpr *E) { return true; }
  bool TraverseCapturedStmt(CapturedStmt *) { return true; }
  bool VisitCXXThrowExpr(CXXThrowExpr *E) {
    CXXTryStmt *TS = getEnclosingTryBlock(E, Body, PMap);

    // - If it is inside a spawning try-block, then an implicit sync is needed.
    //
    // - If it is inside a non-spawning try-block, then no implicit sync
    //   is needed.
    //
    // - If it is not inside a try-block, then an implicit sync is needed only
    //   if this function needs an implicit sync.
    //
    if ( (TS && SyncSet.count(TS)) || (!TS && NeedsSync) )
      SyncSet.insert(E);

    return true;
  }
};
} // namespace

/// \brief Analyze the function AST and decide if
/// - this function needs an implicit sync on exit,
/// - a try-block needs an implicit sync on exit,
/// - a throw expression needs an implicit sync prior to throw.
///
void CGCilkImplicitSyncInfo::analyze() {
  assert(CGF.getLangOpts().CilkPlus && "Not compiling a cilk plus program");
  Stmt *Body = 0;

  const Decl *D = CGF.CurCodeDecl;
  if (D && D->isSpawning()) {
    assert(D->getBody() && "empty body unexpected");
    Body = const_cast<Stmt *>(D->getBody());
  }

  if (!Body)
    return;

  // The following function 'foo' does not need an implicit on exit.
  //
  // void foo() {
  //   try {
  //     _Cilk_spawn bar();
  //   } catch (...) {
  //     return;
  //   }
  // }
  //
  ParentMap PMap(Body);

  // Check if the spawning function or a try-block needs an implicit syncs,
  // and the set of CXXTryStmt's is the analysis results.
  TryStmtAnalyzer Analyzer(Body, PMap, SyncSet);
  NeedsImplicitSync = Analyzer.needsImplicitSync();

  // Traverse and find all CXXThrowExpr's which needs an implicit sync, and
  // the results are inserted to `SyncSet`.
  ThrowExprAnalyzer Analyzer2(Body, PMap, SyncSet, NeedsImplicitSync);
}

CGCilkImplicitSyncInfo *CreateCilkImplicitSyncInfo(CodeGenFunction &CGF) {
  return new CGCilkImplicitSyncInfo(CGF);
}

} // namespace CodeGen
} // namespace clang
