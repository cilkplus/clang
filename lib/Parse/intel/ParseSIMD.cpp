//===--- ParseSIMD.cpp - simd loop parsing --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements parsing of simd loops.
//
//===----------------------------------------------------------------------===//
#if INTEL_SPECIFIC_CILKPLUS

#include "clang/Parse/Parser.h"
#include "clang/Sema/Scope.h"
#include "RAIIObjectsForParser.h"

using namespace clang;

typedef llvm::SmallVector<DeclarationNameInfo, 5> SIMDVariableList;

template <typename T>
bool ParseSIMDListItems(Parser &P, Sema &S, T &ItemParser) {
  const Token &Tok = P.getCurToken();

  while (1) {
    bool ParsedItem = ItemParser.Parse(P, S);

    if (!ParsedItem) {
      while (P.getCurToken().isNot(tok::eof) &&
             !P.SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_simd_end,
                          Parser::StopBeforeMatch));
    }

    if (Tok.is(tok::r_paren) || Tok.is(tok::annot_pragma_simd_end))
      return ParsedItem;

    if (Tok.isNot(tok::comma))
      P.Diag(Tok, diag::err_expected_comma);
    else
      P.ConsumeToken();
  }
}

// Parse a list of the form:
//   '(' { item ',' }* item ')'
template <typename T>
bool ParseSIMDList(Parser &P, Sema &S, T &ItemParser) {
  BalancedDelimiterTracker BDT(P, tok::l_paren);
  if (BDT.expectAndConsume(diag::err_expected_lparen))
    return false;

  bool ParsedLastItem = ParseSIMDListItems(P, S, ItemParser);

  const Token &Tok = P.getCurToken();
  assert(Tok.is(tok::annot_pragma_simd_end) || Tok.is(tok::r_paren));

  if (Tok.is(tok::annot_pragma_simd_end)) {
    if (ParsedLastItem)
      P.Diag(Tok, diag::err_expected_rparen);
    return false;
  } else {
    BDT.consumeClose();
    return !BDT.getCloseLocation().isInvalid();
  }
}

template <typename T>
static bool ParseSIMDExpression(Parser &P, Sema &S, T &ItemParser) {
  BalancedDelimiterTracker BDT(P, tok::l_paren);
  if (BDT.expectAndConsume(diag::err_expected_lparen))
    return false;

  bool ParsedItem = ItemParser.Parse(P, S);
  if (!ParsedItem) {
    while (P.getCurToken().isNot(tok::eof) &&
           !P.SkipUntil(tok::r_paren, tok::annot_pragma_simd_end,
                        Parser::StopBeforeMatch));
    return false;
  }

  const Token &Tok = P.getCurToken();
  if (Tok.isNot(tok::r_paren)) {
    if (ParsedItem) {
      P.Diag(Tok, diag::err_expected_rparen);
      P.Diag(BDT.getOpenLocation(), diag::note_matching) << "'('";
      while (P.getCurToken().isNot(tok::eof) &&
             !P.SkipUntil(tok::annot_pragma_simd_end, Parser::StopBeforeMatch));
    }
    return false;
  } else {
    BDT.consumeClose();
    return !BDT.getCloseLocation().isInvalid();
  }
}

namespace {

// Helper to parse an item of the vectorlength clause.
struct SIMDVectorLengthItemParser {
  Expr *E;
  bool Parse(Parser &P, Sema &S) {
    ExprResult C = P.ParseConstantExpression();
    if (C.isInvalid())
      return false;
    E = C.get();
    return E;
  }
};

// Helper to parse an item of the linear clause.
struct SIMDLinearItemParser {
  SmallVector<Expr *, 8> Exprs;
  bool Parse(Parser &P, Sema &S) {
    ExprResult D = P.ParseConstantExpression();
    if (D.isInvalid())
      return false;
    Expr *E = D.get();
    if (!isa<DeclRefExpr>(E)) {
      P.Diag(E->getLocStart(), diag::err_pragma_simd_invalid_linear_var);
      return false;
    }

    Expr *Step = 0;
    const Token &Tok = P.getCurToken();
    if (Tok.is(tok::colon)) {
      P.ConsumeToken();
      ExprResult C = P.ParseConstantExpression();
      if (C.isInvalid())
        return false;
      Step = C.get();
    }
    Exprs.push_back(E);
    Exprs.push_back(Step);
    return true;
  }
};

// Helper to parse an item of the [first, last]private clauses.
struct SIMDPrivateItemParser {
  SmallVector<Expr *, 2> Exprs;
  Sema::SIMDPrivateKind Kind;
  explicit SIMDPrivateItemParser(Sema::SIMDPrivateKind Kind) : Kind(Kind) { }

  bool Parse(Parser &P, Sema &S) {
    CXXScopeSpec SS;
    SourceLocation TemplateKWLoc;
    UnqualifiedId Name;
    if (P.getLangOpts().CPlusPlus &&
        P.ParseOptionalCXXScopeSpecifier(SS, ParsedType(), false))
      return false;

    if (P.ParseUnqualifiedId(SS,
                             false, // EnteringContext
                             false, // AllowDestructorName
                             false, // AllowConstructorName,
                             ParsedType(), TemplateKWLoc, Name))
      return false;

    DeclarationNameInfo NameInfo = S.GetNameFromUnqualifiedId(Name);
    ExprResult D = S.ActOnPragmaSIMDPrivateVariable(SS, NameInfo, Kind);

    if (D.isInvalid())
      return false;

    Exprs.push_back(D.get());
    return true;
  }
};

// Helper to parse an item of the reduction clause.
struct SIMDReductionItemParser {
  bool ParsedOperator;
  SIMDReductionAttr::SIMDReductionKind Operator;
  SmallVector<Expr *, 8> VarExprs;

  SIMDReductionItemParser()
  : ParsedOperator(false) {
  }

  bool Parse(Parser &P, Sema &S) {
    if (!ParsedOperator) {
      ParsedOperator = true;
      const Token &Tok = P.getCurToken();
      switch (Tok.getKind()) {
      case tok::plus:
      case tok::star:
      case tok::minus:
      case tok::amp:
      case tok::pipe:
      case tok::caret:
      case tok::ampamp:
      case tok::pipepipe:
        Operator =
            static_cast<SIMDReductionAttr::SIMDReductionKind>(Tok.getKind());
        P.ConsumeToken();
        break;
      case tok::identifier: {
        IdentifierInfo *II = Tok.getIdentifierInfo();
        if (II->isStr("max"))
          Operator = SIMDReductionAttr::max;
        else if (II->isStr("min"))
          Operator = SIMDReductionAttr::min;
        else {
          P.Diag(Tok, diag::err_simd_expected_reduction_operator);
          return false;
        }
        P.ConsumeToken();
      } break;
      default:
        P.Diag(Tok, diag::err_simd_expected_reduction_operator);
        return false;
      }

      if (Tok.is(tok::colon))
        P.ConsumeToken();
      else {
        P.Diag(Tok, diag::err_expected_colon);
        return false;
      }
    }
    ExprResult V = P.ParseConstantExpression();
    if (V.isInvalid())
      return false;
    VarExprs.push_back(V.get());

    return true;
  }
};

} // namespace

static bool ParseSIMDClauses(Parser &P, Sema &S, SourceLocation BeginLoc,
                             SmallVectorImpl<Attr *> &AttrList) {
  const Token &Tok = P.getCurToken();
  // # pragma simd simd-clauses
  // simd-clauses:
  //   simd-clause
  //   simd-clauses simd-clause
  while (Tok.isNot(tok::annot_pragma_simd_end)) {
    const SourceLocation ILoc = Tok.getLocation();
    const IdentifierInfo *II = Tok.getIdentifierInfo();

    if (!II) {
      P.Diag(Tok, diag::err_expected_ident);
      while (P.getCurToken().isNot(tok::eof) &&
             !P.SkipUntil(tok::annot_pragma_simd_end, Parser::StopBeforeMatch));
      return false;
    }

    AttrResult A = AttrEmpty();
    if (II->isStr("vectorlength")) {
      P.ConsumeToken();
      SIMDVectorLengthItemParser ItemParser;
      if (!ParseSIMDExpression(P, S, ItemParser) || !ItemParser.E)
        return false;
      A = S.ActOnPragmaSIMDLength(ILoc, ItemParser.E);
    }
    else if (II->isStr("linear")) {
      P.ConsumeToken();
      SIMDLinearItemParser ItemParser;
      if (!ParseSIMDList(P, S, ItemParser))
        return false;
      A = S.ActOnPragmaSIMDLinear(ILoc, ItemParser.Exprs);
    }
    else if (II->isStr("private")) {
      P.ConsumeToken();
      Sema::SIMDPrivateKind Kind = Sema::SIMD_Private;
      SIMDPrivateItemParser ItemParser(Kind);
      if (!ParseSIMDList(P, S, ItemParser))
        return false;
      A = S.ActOnPragmaSIMDPrivate(ILoc, ItemParser.Exprs, Kind);
    }
    else if (II->isStr("firstprivate")) {
      P.ConsumeToken();
      Sema::SIMDPrivateKind Kind = Sema::SIMD_FirstPrivate;
      SIMDPrivateItemParser ItemParser(Kind);
      if (!ParseSIMDList(P, S, ItemParser))
        return false;
      A = S.ActOnPragmaSIMDPrivate(ILoc, ItemParser.Exprs, Kind);
    }
    else if (II->isStr("lastprivate")) {
      P.ConsumeToken();
      Sema::SIMDPrivateKind Kind = Sema::SIMD_LastPrivate;
      SIMDPrivateItemParser ItemParser(Kind);
      if (!ParseSIMDList(P, S, ItemParser))
        return false;
      A = S.ActOnPragmaSIMDPrivate(ILoc, ItemParser.Exprs, Kind);
    }
    else if (II->isStr("reduction")) {
      P.ConsumeToken();
      SIMDReductionItemParser ItemParser;
      if (!ParseSIMDList(P, S, ItemParser))
        return false;
      A = S.ActOnPragmaSIMDReduction(ILoc, ItemParser.Operator,
                                     ItemParser.VarExprs);
    }
    else {
      P.Diag(Tok, diag::err_simd_invalid_clause);
      return false;
    }

    if (A.isInvalid())
      return false;
    AttrList.push_back(A.get());
  }

  return true;
}

void Parser::HandlePragmaSIMD() {
  assert(Tok.is(tok::annot_pragma_simd));
  SourceLocation Loc = Tok.getLocation();
  while (Tok.isNot(tok::eof) && !SkipUntil(tok::annot_pragma_simd_end));
  PP.Diag(Loc, diag::err_pragma_simd_expected_for_loop);
}

static void FinishPragmaSIMD(Parser &P, SourceLocation BeginLoc) {
  const Token &Tok = P.getCurToken();
  assert(Tok.is(tok::annot_pragma_simd_end));
  SourceLocation EndLoc = Tok.getLocation();

  const SourceManager &SM = P.getPreprocessor().getSourceManager();
  unsigned StartLineNumber = SM.getSpellingLineNumber(BeginLoc);
  unsigned EndLineNumber = SM.getSpellingLineNumber(EndLoc);
  (void)StartLineNumber;
  (void)EndLineNumber;
  assert(StartLineNumber == EndLineNumber);

  P.ConsumeToken();
}

/// \brief Parse a pragma simd directive.
///
/// '#' 'pragma' 'simd' simd-clauses[opt] new-line for-statement
///
/// simd-clauses:
///   simd-clause
///   simd-clauses simd-clause
///
/// simd-clause:
///   vectorlength-clause
///   linear-clause
///   openmp-data-clause
///
/// vectorlength-clause:
///   'vectorlength' '(' constant-expression ')'
///
/// linear-clause:
///   'linear' '(' linear-variable-list ')'
///
/// linear-variable-list:
///   linear-variable
///   linear-variable-list ',' linear-variable
///
/// linear-variable:
///   id-expression
///   id-expression ':' linear-step
///
/// linear-step:
///   conditional-expression
///
/// openmp-data-clause:
///   private-clause
///   firstprivate-clause (deprecated)
///   lastprivate-clause
///   reduction-clause
///
StmtResult Parser::ParseSIMDDirective() {
  assert(Tok.is(tok::annot_pragma_simd));
  SourceLocation Loc = Tok.getLocation();
  SourceLocation HashLoc = Loc;
  ConsumeToken();

  SmallVector<Attr *, 4> SIMDAttrList;
  SIMDAttrList.push_back(::new SIMDAttr(Loc, Actions.Context, 0));

  if (!ParseSIMDClauses(*this, Actions, Loc, SIMDAttrList)) {
    while (Tok.isNot(tok::eof) &&
           !SkipUntil(tok::annot_pragma_simd_end, StopBeforeMatch));
    FinishPragmaSIMD(*this, Loc);
    return StmtError();
  }

  FinishPragmaSIMD(*this, Loc);

  StmtResult Pragma;
  // Parse the following statement.
  if (!Tok.is(tok::kw_for)) {
    PP.Diag(Loc, diag::err_pragma_simd_expected_for_loop);
    return StmtEmpty();
  }

  // FIXME: refactor the common code with _Cilk_for statement parsing.
  SourceLocation ForLoc = ConsumeToken();  // eat 'for'.

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "for";
    while(Tok.isNot(tok::eof) && !SkipUntil(tok::semi));
    return StmtError();
  }

  bool C99orCXXorObjC = getLangOpts().C99 || getLangOpts().CPlusPlus ||
                        getLangOpts().ObjC1;

  // Start the loop scope.
  //
  // A program contains a return, break or goto statement that would transfer
  // control into or out of a simd for loop is ill-formed.
  //
  unsigned ScopeFlags = Scope::ContinueScope;
  if (C99orCXXorObjC)
    ScopeFlags |= Scope::DeclScope | Scope::ControlScope;

  ParseScope ForScope(this, ScopeFlags);

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteOrdinaryName(getCurScope(),
                                     C99orCXXorObjC ? Sema::PCC_ForInit
                                                    : Sema::PCC_Expression);
    cutOffParsing();
    return StmtError();
  }

  ParsedAttributesWithRange attrs(AttrFactory);
  MaybeParseCXX11Attributes(attrs);

  // 'for' '(' for-init-stmt
  // 'for' '(' assignment-expr;
  StmtResult FirstPart;

  if (Tok.is(tok::semi)) {  // "for (;"
    ProhibitAttributes(attrs);
    // no initialization, eat the ';'.
    Diag(Tok, diag::err_simd_for_missing_initialization);
    ConsumeToken();
  } else if (isForInitDeclaration()) {  // for (int i = 0;
    // Parse declaration, which eats the ';'.
    if (!C99orCXXorObjC)   // Use of C99-style for loops in C90 mode?
      Diag(Tok, diag::ext_c99_variable_decl_in_for_loop);

    SourceLocation DeclStart = Tok.getLocation();
    SourceLocation DeclEnd;

    // Still use Declarator::ForContext.
    DeclGroupPtrTy DG = ParseSimpleDeclaration(Declarator::ForContext,
                                               DeclEnd, attrs,
                                               /*RequireSemi*/false,
                                               /*ForRangeInit*/0);
    FirstPart = Actions.ActOnDeclStmt(DG, DeclStart, Tok.getLocation());

    if(Tok.is(tok::semi))
      ConsumeToken(); // Eat the ';'.
    else
      Diag(Tok, diag::err_simd_for_missing_semi);
  } else {
    ProhibitAttributes(attrs);
    ExprResult E = ParseExpression();

    if (!E.isInvalid())
      FirstPart = Actions.ActOnExprStmt(E);

    if (Tok.is(tok::semi))
      ConsumeToken(); // Eat the ';'
    else if (!E.isInvalid())
      Diag(Tok, diag::err_simd_for_missing_semi);
    else {
      // Skip until semicolon or rparen, don't consume it.
      while (Tok.isNot(tok::eof) &&
             !SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch));
      if (Tok.is(tok::semi))
        ConsumeToken();
    }
  }

  // 'for' '(' for-init-stmt condition ;
  // 'for' '(' assignment-expr; condition ;
  FullExprArg SecondPart(Actions);
  FullExprArg ThirdPart(Actions);

  if (Tok.is(tok::r_paren)) { // for (...;)
    Diag(Tok, diag::err_simd_for_missing_condition);
    Diag(Tok, diag::err_simd_for_missing_increment);
  } else {
    if (Tok.is(tok::semi)) {  // for (...;;
      // No condition part.
      Diag(Tok, diag::err_simd_for_missing_condition);
      ConsumeToken(); // Eat the ';'
    } else {
      ExprResult E = ParseExpression();
      if (!E.isInvalid())
        E = Actions.ActOnBooleanCondition(getCurScope(), ForLoc, E.get());
      SecondPart = Actions.MakeFullExpr(E.get(), ForLoc);

      if (Tok.isNot(tok::semi)) {
        if (!E.isInvalid())
          Diag(Tok, diag::err_simd_for_missing_semi);
        else
          // Skip until semicolon or rparen, don't consume it.
          while (Tok.isNot(tok::eof) &&
                 !SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch));
      }

      if (Tok.is(tok::semi))
        ConsumeToken();
    }

    // Parse the third part.
    if (Tok.is(tok::r_paren)) { // for (...;...;)
      // No increment part
      Diag(Tok, diag::err_simd_for_missing_increment);
    } else {
      ExprResult E = ParseExpression();
      // FIXME: The C++11 standard doesn't actually say that this is a
      // discarded-value expression, but it clearly should be.
      ThirdPart = Actions.MakeFullDiscardedValueExpr(E.get());
    }
  }

  // Match the ')'.
  T.consumeClose();

  // Start capturing variables inside the loop body.
  Actions.ActOnStartOfSIMDForStmt(Loc, getCurScope(), SIMDAttrList);

  ParseScope InnerScope(this, Scope::BlockScope | Scope::FnScope |
                              Scope::DeclScope | Scope::ContinueScope);


  // Read the body statement.
  StmtResult Body(ParseStatement());

  // Pop the body scope if needed.
  InnerScope.Exit();

  // Leave the for-scope.
  ForScope.Exit();

  if (!FirstPart.isUsable() || !SecondPart.get() || !ThirdPart.get() ||
      !Body.isUsable()) {
    Actions.ActOnSIMDForStmtError();
    return StmtError();
  }

  StmtResult Result = Actions.ActOnSIMDForStmt(HashLoc, SIMDAttrList, ForLoc,
                                               T.getOpenLocation(),
                                               FirstPart.get(), SecondPart,
                                               ThirdPart, T.getCloseLocation(),
                                               Body.get());

  if (Result.isInvalid()) {
    Actions.ActOnSIMDForStmtError();
    return StmtError();
  }

  if (Pragma.isUsable()) {
    Sema::CompoundScopeRAII CompoundScope(Actions);
    Stmt *Args[] = {Pragma.get(), Result.get()};
    return Actions.ActOnCompoundStmt(SourceLocation(), SourceLocation(), Args,
                                     false);
  }

  return Result;
}

#endif // INTEL_SPECIFIC_CILKPLUS
