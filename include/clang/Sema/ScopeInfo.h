//===--- ScopeInfo.h - Information about a semantic context -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines FunctionScopeInfo and its subclasses, which contain
// information about a single function, block, lambda, or method body.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SCOPE_INFO_H
#define LLVM_CLANG_SEMA_SCOPE_INFO_H

#include "clang/AST/Type.h"
#include "clang/Basic/CapturedStmt.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>

namespace clang {

class Decl;
class BlockDecl;
class CapturedDecl;
class CXXMethodDecl;
class FieldDecl;
class ObjCPropertyDecl;
class IdentifierInfo;
class ImplicitParamDecl;
class LabelDecl;
class ReturnStmt;
class Scope;
class SwitchStmt;
class TemplateTypeParmDecl;
class TemplateParameterList;
class VarDecl;
class DeclRefExpr;
class MemberExpr;
class ObjCIvarRefExpr;
class ObjCPropertyRefExpr;
class ObjCMessageExpr;

namespace sema {

/// \brief Contains information about the compound statement currently being
/// parsed.
class CompoundScopeInfo {
public:
  CompoundScopeInfo()
    : HasEmptyLoopBodies(false) { }

  /// \brief Whether this compound stamement contains `for' or `while' loops
  /// with empty bodies.
  bool HasEmptyLoopBodies;

  void setHasEmptyLoopBodies() {
    HasEmptyLoopBodies = true;
  }
};

class PossiblyUnreachableDiag {
public:
  PartialDiagnostic PD;
  SourceLocation Loc;
  const Stmt *stmt;
  
  PossiblyUnreachableDiag(const PartialDiagnostic &PD, SourceLocation Loc,
                          const Stmt *stmt)
    : PD(PD), Loc(Loc), stmt(stmt) {}
};
    
/// \brief Retains information about a function, method, or block that is
/// currently being parsed.
class FunctionScopeInfo {
protected:
  enum ScopeKind {
    SK_Function,
    SK_Block,
    SK_Lambda,
    SK_CapturedRegion
  };
  
public:
  /// \brief What kind of scope we are describing.
  ///
  ScopeKind Kind;

  /// \brief Whether this function contains a VLA, \@try, try, C++
  /// initializer, or anything else that can't be jumped past.
  bool HasBranchProtectedScope;

  /// \brief Whether this function contains any switches or direct gotos.
  bool HasBranchIntoScope;

  /// \brief Whether this function contains any indirect gotos.
  bool HasIndirectGoto;

  /// \brief Whether a statement was dropped because it was invalid.
  bool HasDroppedStmt;

  /// A flag that is set when parsing a method that must call super's
  /// implementation, such as \c -dealloc, \c -finalize, or any method marked
  /// with \c __attribute__((objc_requires_super)).
  bool ObjCShouldCallSuper;

  /// \brief Used to determine if errors occurred in this function or block.
  DiagnosticErrorTrap ErrorTrap;

  /// SwitchStack - This is the current set of active switch statements in the
  /// block.
  SmallVector<SwitchStmt*, 8> SwitchStack;

  /// \brief The list of return statements that occur within the function or
  /// block, if there is any chance of applying the named return value
  /// optimization, or if we need to infer a return type.
  SmallVector<ReturnStmt*, 4> Returns;

  /// \brief The stack of currently active compound stamement scopes in the
  /// function.
  SmallVector<CompoundScopeInfo, 4> CompoundScopes;

  /// \brief A list of PartialDiagnostics created but delayed within the
  /// current function scope.  These diagnostics are vetted for reachability
  /// prior to being emitted.
  SmallVector<PossiblyUnreachableDiag, 4> PossiblyUnreachableDiags;

public:
  /// Represents a simple identification of a weak object.
  ///
  /// Part of the implementation of -Wrepeated-use-of-weak.
  ///
  /// This is used to determine if two weak accesses refer to the same object.
  /// Here are some examples of how various accesses are "profiled":
  ///
  /// Access Expression |     "Base" Decl     |          "Property" Decl
  /// :---------------: | :-----------------: | :------------------------------:
  /// self.property     | self (VarDecl)      | property (ObjCPropertyDecl)
  /// self.implicitProp | self (VarDecl)      | -implicitProp (ObjCMethodDecl)
  /// self->ivar.prop   | ivar (ObjCIvarDecl) | prop (ObjCPropertyDecl)
  /// cxxObj.obj.prop   | obj (FieldDecl)     | prop (ObjCPropertyDecl)
  /// [self foo].prop   | 0 (unknown)         | prop (ObjCPropertyDecl)
  /// self.prop1.prop2  | prop1 (ObjCPropertyDecl)    | prop2 (ObjCPropertyDecl)
  /// MyClass.prop      | MyClass (ObjCInterfaceDecl) | -prop (ObjCMethodDecl)
  /// weakVar           | 0 (known)           | weakVar (VarDecl)
  /// self->weakIvar    | self (VarDecl)      | weakIvar (ObjCIvarDecl)
  ///
  /// Objects are identified with only two Decls to make it reasonably fast to
  /// compare them.
  class WeakObjectProfileTy {
    /// The base object decl, as described in the class documentation.
    ///
    /// The extra flag is "true" if the Base and Property are enough to uniquely
    /// identify the object in memory.
    ///
    /// \sa isExactProfile()
    typedef llvm::PointerIntPair<const NamedDecl *, 1, bool> BaseInfoTy;
    BaseInfoTy Base;

    /// The "property" decl, as described in the class documentation.
    ///
    /// Note that this may not actually be an ObjCPropertyDecl, e.g. in the
    /// case of "implicit" properties (regular methods accessed via dot syntax).
    const NamedDecl *Property;

    /// Used to find the proper base profile for a given base expression.
    static BaseInfoTy getBaseInfo(const Expr *BaseE);

    // For use in DenseMap.
    friend class DenseMapInfo;
    inline WeakObjectProfileTy();
    static inline WeakObjectProfileTy getSentinel();

  public:
    WeakObjectProfileTy(const ObjCPropertyRefExpr *RE);
    WeakObjectProfileTy(const Expr *Base, const ObjCPropertyDecl *Property);
    WeakObjectProfileTy(const DeclRefExpr *RE);
    WeakObjectProfileTy(const ObjCIvarRefExpr *RE);

    const NamedDecl *getBase() const { return Base.getPointer(); }
    const NamedDecl *getProperty() const { return Property; }

    /// Returns true if the object base specifies a known object in memory,
    /// rather than, say, an instance variable or property of another object.
    ///
    /// Note that this ignores the effects of aliasing; that is, \c foo.bar is
    /// considered an exact profile if \c foo is a local variable, even if
    /// another variable \c foo2 refers to the same object as \c foo.
    ///
    /// For increased precision, accesses with base variables that are
    /// properties or ivars of 'self' (e.g. self.prop1.prop2) are considered to
    /// be exact, though this is not true for arbitrary variables
    /// (foo.prop1.prop2).
    bool isExactProfile() const {
      return Base.getInt();
    }

    bool operator==(const WeakObjectProfileTy &Other) const {
      return Base == Other.Base && Property == Other.Property;
    }

    // For use in DenseMap.
    // We can't specialize the usual llvm::DenseMapInfo at the end of the file
    // because by that point the DenseMap in FunctionScopeInfo has already been
    // instantiated.
    class DenseMapInfo {
    public:
      static inline WeakObjectProfileTy getEmptyKey() {
        return WeakObjectProfileTy();
      }
      static inline WeakObjectProfileTy getTombstoneKey() {
        return WeakObjectProfileTy::getSentinel();
      }

      static unsigned getHashValue(const WeakObjectProfileTy &Val) {
        typedef std::pair<BaseInfoTy, const NamedDecl *> Pair;
        return llvm::DenseMapInfo<Pair>::getHashValue(Pair(Val.Base,
                                                           Val.Property));
      }

      static bool isEqual(const WeakObjectProfileTy &LHS,
                          const WeakObjectProfileTy &RHS) {
        return LHS == RHS;
      }
    };
  };

  /// Represents a single use of a weak object.
  ///
  /// Stores both the expression and whether the access is potentially unsafe
  /// (i.e. it could potentially be warned about).
  ///
  /// Part of the implementation of -Wrepeated-use-of-weak.
  class WeakUseTy {
    llvm::PointerIntPair<const Expr *, 1, bool> Rep;
  public:
    WeakUseTy(const Expr *Use, bool IsRead) : Rep(Use, IsRead) {}

    const Expr *getUseExpr() const { return Rep.getPointer(); }
    bool isUnsafe() const { return Rep.getInt(); }
    void markSafe() { Rep.setInt(false); }

    bool operator==(const WeakUseTy &Other) const {
      return Rep == Other.Rep;
    }
  };

  /// Used to collect uses of a particular weak object in a function body.
  ///
  /// Part of the implementation of -Wrepeated-use-of-weak.
  typedef SmallVector<WeakUseTy, 4> WeakUseVector;

  /// Used to collect all uses of weak objects in a function body.
  ///
  /// Part of the implementation of -Wrepeated-use-of-weak.
  typedef llvm::SmallDenseMap<WeakObjectProfileTy, WeakUseVector, 8,
                              WeakObjectProfileTy::DenseMapInfo>
          WeakObjectUseMap;

private:
  /// Used to collect all uses of weak objects in this function body.
  ///
  /// Part of the implementation of -Wrepeated-use-of-weak.
  WeakObjectUseMap WeakObjectUses;

public:
  /// Record that a weak object was accessed.
  ///
  /// Part of the implementation of -Wrepeated-use-of-weak.
  template <typename ExprT>
  inline void recordUseOfWeak(const ExprT *E, bool IsRead = true);

  void recordUseOfWeak(const ObjCMessageExpr *Msg,
                       const ObjCPropertyDecl *Prop);

  /// Record that a given expression is a "safe" access of a weak object (e.g.
  /// assigning it to a strong variable.)
  ///
  /// Part of the implementation of -Wrepeated-use-of-weak.
  void markSafeWeakUse(const Expr *E);

  const WeakObjectUseMap &getWeakObjectUses() const {
    return WeakObjectUses;
  }

  void setHasBranchIntoScope() {
    HasBranchIntoScope = true;
  }

  void setHasBranchProtectedScope() {
    HasBranchProtectedScope = true;
  }

  void setHasIndirectGoto() {
    HasIndirectGoto = true;
  }

  void setHasDroppedStmt() {
    HasDroppedStmt = true;
  }

  bool NeedsScopeChecking() const {
    return !HasDroppedStmt &&
        (HasIndirectGoto ||
          (HasBranchProtectedScope && HasBranchIntoScope));
  }
  
  FunctionScopeInfo(DiagnosticsEngine &Diag)
    : Kind(SK_Function),
      HasBranchProtectedScope(false),
      HasBranchIntoScope(false),
      HasIndirectGoto(false),
      HasDroppedStmt(false),
      ObjCShouldCallSuper(false),
      ErrorTrap(Diag) { }

  virtual ~FunctionScopeInfo();

  /// \brief Clear out the information in this function scope, making it
  /// suitable for reuse.
  void Clear();
};

class CapturingScopeInfo : public FunctionScopeInfo {
public:
  enum ImplicitCaptureStyle {
    ImpCap_None, ImpCap_LambdaByval, ImpCap_LambdaByref, ImpCap_Block,
    ImpCap_CapturedRegion
  };

  ImplicitCaptureStyle ImpCaptureStyle;

  class Capture {
    // There are three categories of capture: capturing 'this', capturing
    // local variables, and C++1y initialized captures (which can have an
    // arbitrary initializer, and don't really capture in the traditional
    // sense at all).
    //
    // There are three ways to capture a local variable:
    //  - capture by copy in the C++11 sense,
    //  - capture by reference in the C++11 sense, and
    //  - __block capture.
    // Lambdas explicitly specify capture by copy or capture by reference.
    // For blocks, __block capture applies to variables with that annotation,
    // variables of reference type are captured by reference, and other
    // variables are captured by copy.
    enum CaptureKind {
      Cap_ByCopy, Cap_ByRef, Cap_Block, Cap_This
    };

    /// The variable being captured (if we are not capturing 'this') and whether
    /// this is a nested capture.
    llvm::PointerIntPair<VarDecl*, 1, bool> VarAndNested;

    /// Expression to initialize a field of the given type, and the kind of
    /// capture (if this is a capture and not an init-capture). The expression
    /// is only required if we are capturing ByVal and the variable's type has
    /// a non-trivial copy constructor.
    llvm::PointerIntPair<Expr*, 2, CaptureKind> InitExprAndCaptureKind;

    /// \brief The source location at which the first capture occurred.
    SourceLocation Loc;

    /// \brief The location of the ellipsis that expands a parameter pack.
    SourceLocation EllipsisLoc;

    /// \brief The type as it was captured, which is in effect the type of the
    /// non-static data member that would hold the capture.
    QualType CaptureType;

  public:
    Capture(VarDecl *Var, bool Block, bool ByRef, bool IsNested,
            SourceLocation Loc, SourceLocation EllipsisLoc,
            QualType CaptureType, Expr *Cpy)
        : VarAndNested(Var, IsNested),
          InitExprAndCaptureKind(Cpy, Block ? Cap_Block :
                                      ByRef ? Cap_ByRef : Cap_ByCopy),
          Loc(Loc), EllipsisLoc(EllipsisLoc), CaptureType(CaptureType) {}

    enum IsThisCapture { ThisCapture };
    Capture(IsThisCapture, bool IsNested, SourceLocation Loc,
            QualType CaptureType, Expr *Cpy)
        : VarAndNested(0, IsNested),
          InitExprAndCaptureKind(Cpy, Cap_This),
          Loc(Loc), EllipsisLoc(), CaptureType(CaptureType) {}

    bool isThisCapture() const {
      return InitExprAndCaptureKind.getInt() == Cap_This;
    }
    bool isVariableCapture() const {
      return InitExprAndCaptureKind.getInt() != Cap_This;
    }
    bool isCopyCapture() const {
      return InitExprAndCaptureKind.getInt() == Cap_ByCopy;
    }
    bool isReferenceCapture() const {
      return InitExprAndCaptureKind.getInt() == Cap_ByRef;
    }
    bool isBlockCapture() const {
      return InitExprAndCaptureKind.getInt() == Cap_Block;
    }
    bool isNested() { return VarAndNested.getInt(); }

    VarDecl *getVariable() const {
      return VarAndNested.getPointer();
    }
    
    /// \brief Retrieve the location at which this variable was captured.
    SourceLocation getLocation() const { return Loc; }
    
    /// \brief Retrieve the source location of the ellipsis, whose presence
    /// indicates that the capture is a pack expansion.
    SourceLocation getEllipsisLoc() const { return EllipsisLoc; }
    
    /// \brief Retrieve the capture type for this capture, which is effectively
    /// the type of the non-static data member in the lambda/block structure
    /// that would store this capture.
    QualType getCaptureType() const { return CaptureType; }
    
    Expr *getInitExpr() const {
      return InitExprAndCaptureKind.getPointer();
    }
  };

  CapturingScopeInfo(DiagnosticsEngine &Diag, ImplicitCaptureStyle Style)
    : FunctionScopeInfo(Diag), ImpCaptureStyle(Style), CXXThisCaptureIndex(0),
      HasImplicitReturnType(false)
     {}

  /// CaptureMap - A map of captured variables to (index+1) into Captures.
  llvm::DenseMap<VarDecl*, unsigned> CaptureMap;

  /// CXXThisCaptureIndex - The (index+1) of the capture of 'this';
  /// zero if 'this' is not captured.
  unsigned CXXThisCaptureIndex;

  /// Captures - The captures.
  SmallVector<Capture, 4> Captures;

  /// \brief - Whether the target type of return statements in this context
  /// is deduced (e.g. a lambda or block with omitted return type).
  bool HasImplicitReturnType;

  /// ReturnType - The target type of return statements in this context,
  /// or null if unknown.
  QualType ReturnType;

  void addCapture(VarDecl *Var, bool isBlock, bool isByref, bool isNested,
                  SourceLocation Loc, SourceLocation EllipsisLoc, 
                  QualType CaptureType, Expr *Cpy) {
    Captures.push_back(Capture(Var, isBlock, isByref, isNested, Loc, 
                               EllipsisLoc, CaptureType, Cpy));
    CaptureMap[Var] = Captures.size();
  }

  void addThisCapture(bool isNested, SourceLocation Loc, QualType CaptureType,
                      Expr *Cpy);

  /// \brief Determine whether the C++ 'this' is captured.
  bool isCXXThisCaptured() const { return CXXThisCaptureIndex != 0; }
  
  /// \brief Retrieve the capture of C++ 'this', if it has been captured.
  Capture &getCXXThisCapture() {
    assert(isCXXThisCaptured() && "this has not been captured");
    return Captures[CXXThisCaptureIndex - 1];
  }
  
  /// \brief Determine whether the given variable has been captured.
  bool isCaptured(VarDecl *Var) const {
    return CaptureMap.count(Var);
  }
  
  /// \brief Retrieve the capture of the given variable, if it has been
  /// captured already.
  Capture &getCapture(VarDecl *Var) {
    assert(isCaptured(Var) && "Variable has not been captured");
    return Captures[CaptureMap[Var] - 1];
  }

  const Capture &getCapture(VarDecl *Var) const {
    llvm::DenseMap<VarDecl*, unsigned>::const_iterator Known
      = CaptureMap.find(Var);
    assert(Known != CaptureMap.end() && "Variable has not been captured");
    return Captures[Known->second - 1];
  }

  static bool classof(const FunctionScopeInfo *FSI) { 
    return FSI->Kind == SK_Block || FSI->Kind == SK_Lambda
                                 || FSI->Kind == SK_CapturedRegion;
  }
};

/// \brief Retains information about a block that is currently being parsed.
class BlockScopeInfo : public CapturingScopeInfo {
public:
  BlockDecl *TheDecl;
  
  /// TheScope - This is the scope for the block itself, which contains
  /// arguments etc.
  Scope *TheScope;

  /// BlockType - The function type of the block, if one was given.
  /// Its return type may be BuiltinType::Dependent.
  QualType FunctionType;

  BlockScopeInfo(DiagnosticsEngine &Diag, Scope *BlockScope, BlockDecl *Block)
    : CapturingScopeInfo(Diag, ImpCap_Block), TheDecl(Block),
      TheScope(BlockScope)
  {
    Kind = SK_Block;
  }

  virtual ~BlockScopeInfo();

  static bool classof(const FunctionScopeInfo *FSI) { 
    return FSI->Kind == SK_Block; 
  }
};

/// \brief Retains information about a captured region.
class CapturedRegionScopeInfo: public CapturingScopeInfo {
public:
  /// \brief The CapturedDecl for this statement.
  CapturedDecl *TheCapturedDecl;
  /// \brief The captured record type.
  RecordDecl *TheRecordDecl;
  /// \brief This is the enclosing scope of the captured region.
  Scope *TheScope;
  /// \brief The implicit parameter for the captured variables.
  ImplicitParamDecl *ContextParam;
  /// \brief The kind of captured region.
  CapturedRegionKind CapRegionKind;

  CapturedRegionScopeInfo(DiagnosticsEngine &Diag, Scope *S, CapturedDecl *CD,
                          RecordDecl *RD, ImplicitParamDecl *Context,
                          CapturedRegionKind K)
    : CapturingScopeInfo(Diag, ImpCap_CapturedRegion),
      TheCapturedDecl(CD), TheRecordDecl(RD), TheScope(S),
      ContextParam(Context), CapRegionKind(K)
  {
    Kind = SK_CapturedRegion;
  }

  virtual ~CapturedRegionScopeInfo();

  /// \brief A descriptive name for the kind of captured region this is.
  StringRef getRegionName() const {
    switch (CapRegionKind) {
    case CR_Default:
      return "default captured statement";
    case CR_OpenMP:
      return "OpenMP region";
    }
    llvm_unreachable("Invalid captured region kind!");
  }

  static bool classof(const FunctionScopeInfo *FSI) {
    return FSI->Kind == SK_CapturedRegion;
  }
};

class LambdaScopeInfo : public CapturingScopeInfo {
public:
  /// \brief The class that describes the lambda.
  CXXRecordDecl *Lambda;

  /// \brief The lambda's compiler-generated \c operator().
  CXXMethodDecl *CallOperator;

  /// \brief Source range covering the lambda introducer [...].
  SourceRange IntroducerRange;

  /// \brief Source location of the '&' or '=' specifying the default capture
  /// type, if any.
  SourceLocation CaptureDefaultLoc;

  /// \brief The number of captures in the \c Captures list that are
  /// explicit captures.
  unsigned NumExplicitCaptures;

  /// \brief Whether this is a mutable lambda.
  bool Mutable;

  /// \brief Whether the (empty) parameter list is explicit.
  bool ExplicitParams;

  /// \brief Whether any of the capture expressions requires cleanups.
  bool ExprNeedsCleanups;

  /// \brief Whether the lambda contains an unexpanded parameter pack.
  bool ContainsUnexpandedParameterPack;

  /// \brief Variables used to index into by-copy array captures.
  SmallVector<VarDecl *, 4> ArrayIndexVars;

  /// \brief Offsets into the ArrayIndexVars array at which each capture starts
  /// its list of array index variables.
  SmallVector<unsigned, 4> ArrayIndexStarts;
  
  /// \brief If this is a generic lambda, use this as the depth of 
  /// each 'auto' parameter, during initial AST construction.
  unsigned AutoTemplateParameterDepth;

  /// \brief Store the list of the auto parameters for a generic lambda.
  /// If this is a generic lambda, store the list of the auto 
  /// parameters converted into TemplateTypeParmDecls into a vector
  /// that can be used to construct the generic lambda's template
  /// parameter list, during initial AST construction.
  SmallVector<TemplateTypeParmDecl*, 4> AutoTemplateParams;

  /// If this is a generic lambda, and the template parameter
  /// list has been created (from the AutoTemplateParams) then
  /// store a reference to it (cache it to avoid reconstructing it).
  TemplateParameterList *GLTemplateParameterList;
  
  /// \brief Contains all variable-referring-expressions (i.e. DeclRefExprs
  ///  or MemberExprs) that refer to local variables in a generic lambda
  ///  or a lambda in a potentially-evaluated-if-used context.
  ///  
  ///  Potentially capturable variables of a nested lambda that might need 
  ///   to be captured by the lambda are housed here.  
  ///  This is specifically useful for generic lambdas or
  ///  lambdas within a a potentially evaluated-if-used context.
  ///  If an enclosing variable is named in an expression of a lambda nested
  ///  within a generic lambda, we don't always know know whether the variable 
  ///  will truly be odr-used (i.e. need to be captured) by that nested lambda,
  ///  until its instantiation. But we still need to capture it in the 
  ///  enclosing lambda if all intervening lambdas can capture the variable.

  llvm::SmallVector<Expr*, 4> PotentiallyCapturingExprs;

  /// \brief Contains all variable-referring-expressions that refer
  ///  to local variables that are usable as constant expressions and
  ///  do not involve an odr-use (they may still need to be captured
  ///  if the enclosing full-expression is instantiation dependent).
  llvm::SmallSet<Expr*, 8> NonODRUsedCapturingExprs; 

  SourceLocation PotentialThisCaptureLocation;

  LambdaScopeInfo(DiagnosticsEngine &Diag)
    : CapturingScopeInfo(Diag, ImpCap_None), Lambda(0),
      CallOperator(0), NumExplicitCaptures(0), Mutable(false),
      ExprNeedsCleanups(false), ContainsUnexpandedParameterPack(false),
      AutoTemplateParameterDepth(0), GLTemplateParameterList(0)
  {
    Kind = SK_Lambda;
  }

  virtual ~LambdaScopeInfo();

  /// \brief Note when all explicit captures have been added.
  void finishedExplicitCaptures() {
    NumExplicitCaptures = Captures.size();
  }

  static bool classof(const FunctionScopeInfo *FSI) {
    return FSI->Kind == SK_Lambda;
  }

  ///
  /// \brief Add a variable that might potentially be captured by the 
  /// lambda and therefore the enclosing lambdas. 
  /// 
  /// This is also used by enclosing lambda's to speculatively capture 
  /// variables that nested lambda's - depending on their enclosing
  /// specialization - might need to capture.
  /// Consider:
  /// void f(int, int); <-- don't capture
  /// void f(const int&, double); <-- capture
  /// void foo() {
  ///   const int x = 10;
  ///   auto L = [=](auto a) { // capture 'x'
  ///      return [=](auto b) { 
  ///        f(x, a);  // we may or may not need to capture 'x'
  ///      };
  ///   };
  /// }
  void addPotentialCapture(Expr *VarExpr) {
    assert(isa<DeclRefExpr>(VarExpr) || isa<MemberExpr>(VarExpr));
    PotentiallyCapturingExprs.push_back(VarExpr);
  }
  
  void addPotentialThisCapture(SourceLocation Loc) {
    PotentialThisCaptureLocation = Loc;
  }
  bool hasPotentialThisCapture() const { 
    return PotentialThisCaptureLocation.isValid(); 
  }

  /// \brief Mark a variable's reference in a lambda as non-odr using.
  ///
  /// For generic lambdas, if a variable is named in a potentially evaluated 
  /// expression, where the enclosing full expression is dependent then we 
  /// must capture the variable (given a default capture).
  /// This is accomplished by recording all references to variables 
  /// (DeclRefExprs or MemberExprs) within said nested lambda in its array of 
  /// PotentialCaptures. All such variables have to be captured by that lambda,
  /// except for as described below.
  /// If that variable is usable as a constant expression and is named in a 
  /// manner that does not involve its odr-use (e.g. undergoes 
  /// lvalue-to-rvalue conversion, or discarded) record that it is so. Upon the
  /// act of analyzing the enclosing full expression (ActOnFinishFullExpr)
  /// if we can determine that the full expression is not instantiation-
  /// dependent, then we can entirely avoid its capture. 
  ///
  ///   const int n = 0;
  ///   [&] (auto x) {
  ///     (void)+n + x;
  ///   };
  /// Interestingly, this strategy would involve a capture of n, even though 
  /// it's obviously not odr-used here, because the full-expression is 
  /// instantiation-dependent.  It could be useful to avoid capturing such
  /// variables, even when they are referred to in an instantiation-dependent
  /// expression, if we can unambiguously determine that they shall never be
  /// odr-used.  This would involve removal of the variable-referring-expression
  /// from the array of PotentialCaptures during the lvalue-to-rvalue 
  /// conversions.  But per the working draft N3797, (post-chicago 2013) we must
  /// capture such variables. 
  /// Before anyone is tempted to implement a strategy for not-capturing 'n',
  /// consider the insightful warning in: 
  ///    /cfe-commits/Week-of-Mon-20131104/092596.html
  /// "The problem is that the set of captures for a lambda is part of the ABI
  ///  (since lambda layout can be made visible through inline functions and the
  ///  like), and there are no guarantees as to which cases we'll manage to build
  ///  an lvalue-to-rvalue conversion in, when parsing a template -- some
  ///  seemingly harmless change elsewhere in Sema could cause us to start or stop
  ///  building such a node. So we need a rule that anyone can implement and get
  ///  exactly the same result".
  ///    
  void markVariableExprAsNonODRUsed(Expr *CapturingVarExpr) {
    assert(isa<DeclRefExpr>(CapturingVarExpr) 
        || isa<MemberExpr>(CapturingVarExpr));
    NonODRUsedCapturingExprs.insert(CapturingVarExpr);
  }
  bool isVariableExprMarkedAsNonODRUsed(Expr *CapturingVarExpr) {
    assert(isa<DeclRefExpr>(CapturingVarExpr) 
      || isa<MemberExpr>(CapturingVarExpr));
    return NonODRUsedCapturingExprs.count(CapturingVarExpr);
  }
  void removePotentialCapture(Expr *E) {
    PotentiallyCapturingExprs.erase(
        std::remove(PotentiallyCapturingExprs.begin(), 
            PotentiallyCapturingExprs.end(), E), 
        PotentiallyCapturingExprs.end());
  }
  void clearPotentialCaptures() {
    PotentiallyCapturingExprs.clear();
    PotentialThisCaptureLocation = SourceLocation();
  }
  unsigned getNumPotentialVariableCaptures() const { 
    return PotentiallyCapturingExprs.size(); 
  }

  bool hasPotentialCaptures() const { 
    return getNumPotentialVariableCaptures() || 
                                  PotentialThisCaptureLocation.isValid(); 
  }
  
  // When passed the index, returns the VarDecl and Expr associated 
  // with the index.
  void getPotentialVariableCapture(unsigned Idx, VarDecl *&VD, Expr *&E);
 
};


FunctionScopeInfo::WeakObjectProfileTy::WeakObjectProfileTy()
  : Base(0, false), Property(0) {}

FunctionScopeInfo::WeakObjectProfileTy
FunctionScopeInfo::WeakObjectProfileTy::getSentinel() {
  FunctionScopeInfo::WeakObjectProfileTy Result;
  Result.Base.setInt(true);
  return Result;
}

template <typename ExprT>
void FunctionScopeInfo::recordUseOfWeak(const ExprT *E, bool IsRead) {
  assert(E);
  WeakUseVector &Uses = WeakObjectUses[WeakObjectProfileTy(E)];
  Uses.push_back(WeakUseTy(E, IsRead));
}

inline void
CapturingScopeInfo::addThisCapture(bool isNested, SourceLocation Loc,
                                   QualType CaptureType, Expr *Cpy) {
  Captures.push_back(Capture(Capture::ThisCapture, isNested, Loc, CaptureType,
                             Cpy));
  CXXThisCaptureIndex = Captures.size();

  if (LambdaScopeInfo *LSI = dyn_cast<LambdaScopeInfo>(this))
    LSI->ArrayIndexStarts.push_back(LSI->ArrayIndexVars.size());
}

} // end namespace sema
} // end namespace clang

#endif

