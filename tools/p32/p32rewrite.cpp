// p32rewrite - finds every pointer declarator in the engine's sources that must become a 32-bit pointer for a
// 64-bit build, and prints where (tools/p32/p32tree.py applies the insertions to a copy of the tree).
//
// Why. The engine is decompiled 32-bit code: struct layouts, literal allocation sizes, raw offsets and the fastfile
// loader all assume 4-byte pointers. A 64-bit build keeps that true by making every pointer OBJECT (struct field,
// global, local, array element, pointee of a pointer) a clang `__ptr32 __uptr` pointer, spelled BO1_P32, and by
// keeping all engine memory below 4 GB. Pointer VALUES passed around stay native: the top-level pointer of a
// function parameter and of a return type is left alone, so signatures still match the C library's
// (qsort comparators, thread entry points) and conversions are implicit both ways.
//
//   p32rewrite -p <build dir with compile_commands.json> --root <repo> [--exclude <dir>]... <sources...>
//
// Two more rewrites make that model hold:
//   - `long` is 32 bits to this code (MSVC), 64 on LP64: the keyword becomes `int` (same width in the text).
//     `long long` and `long double` are other types and are not touched.
//   - LLVM cannot select a call through a 32-bit function pointer (x86-64 and AArch64 both stop in instruction
//     selection). Every indirect call's callee, and every `delete` operand, is wrapped in BO1_NAT(...), which
//     converts to the native pointer type.
//
// Output, one line per edit (duplicates are expected, headers are seen from many translation units; the caller
// dedupes):
//   P <file> <offset of a '*'>             insert " BO1_P32 " after it
//   L <file> <offset of a `long` token>    replace the 4 characters with "int "
//   F <file> <offset of the `long` of a `long double`>    replace the 4 characters with spaces
//   N <file> <begin> <end>                 wrap the expression text [begin, end) in BO1_NAT( ... )
//   D <file> <begin> <end>                 wrap it in BO1_NATD( ... ): a delete operand
//   U <file> <begin> <end>                 wrap it in BO1_U32( ... ): a signed integer about to be cast to a pointer
//   I <file> <begin> <end>                 wrap it in BO1_IDX( ... ): an unsigned 32-bit subscript or pointer offset
//   A <file> <begin> <end>                 wrap it in BO1_ADDR( ... ): an address in a static initializer, which
//                                          LLVM cannot narrow to 32 bits at compile time
//   W <file> <line>                        a pointer left native on purpose (see the notes in the code)
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>

#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace clang;

static llvm::cl::OptionCategory s_category("p32rewrite");
static llvm::cl::opt<std::string> s_root("root", llvm::cl::desc("repository root: only files below it are rewritten"),
    llvm::cl::Required, llvm::cl::cat(s_category));
static llvm::cl::list<std::string> s_longOnly("long-only",
    llvm::cl::desc("excluded directory in which `long` is still rewritten (a C library whose structs the engine embeds)"),
    llvm::cl::cat(s_category));
static llvm::cl::list<std::string> s_localsNative("locals-native",
    llvm::cl::desc("directory whose block-scope pointer variables keep native pointers (platform code)"),
    llvm::cl::cat(s_category));
static llvm::cl::list<std::string> s_exclude("exclude", llvm::cl::desc("directory (relative to the root) to leave alone"),
    llvm::cl::cat(s_category));

namespace
{
class Visitor : public RecursiveASTVisitor<Visitor>
{
public:
    explicit Visitor(ASTContext &context) : m_context(context), m_sm(context.getSourceManager()) {}

    bool shouldVisitTemplateInstantiations() const { return false; }
    bool shouldVisitImplicitCode() const { return false; }

    // The outermost pointer of a parameter is a value, not an object: leave it native. Unless the function takes
    // the parameter's address (`Com_Parse(&text)`): then it is an object like any other, and the star is
    // annotated in this declaration and (through the other translation units' output being merged) in the
    // header's.
    bool VisitParmVarDecl(ParmVarDecl *decl)
    {
        TypeSourceInfo *tsi = decl->getTypeSourceInfo();
        if (!tsi)
            return true;
        TypeLoc loc = Strip(tsi->getTypeLoc());
        if (loc.isNull())
            return true;
        if (auto p = loc.getAs<PointerTypeLoc>())
        {
            const unsigned star = p.getStarLoc().getRawEncoding();
            m_excluded.insert(star);
            if (const auto *fn = dyn_cast_or_null<FunctionDecl>(decl->getDeclContext()))
                m_paramStars.push_back({fn->getCanonicalDecl(), decl->getFunctionScopeIndex(), star,
                    InDirs(decl->getLocation(), s_localsNative)});
        }
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator *op)
    {
        if (op->getOpcode() != UO_AddrOf)
            return true;
        const auto *ref = dyn_cast<DeclRefExpr>(op->getSubExpr()->IgnoreParens());
        if (!ref)
            return true;
        if (const auto *parm = dyn_cast<ParmVarDecl>(ref->getDecl()))
            if (const auto *fn = dyn_cast_or_null<FunctionDecl>(parm->getDeclContext()))
                m_addressTaken.insert({fn->getCanonicalDecl(), parm->getFunctionScopeIndex()});
        return true;
    }

    // Unsigned 32-bit offsets. The decompiler writes `array[viewIndex - 13]` with an unsigned viewIndex: in a
    // 32-bit program the address wraps and that is element -13..-7. In a 64-bit one the index zero-extends and
    // lands 16 GB away. No object of a 32-bit program spans 2 GB, so an unsigned 32-bit offset means exactly what its
    // value as a signed int means: convert it (BO1_IDX).
    // `long` written by the engine (or through its own typedefs: LONG, DWORD) is rewritten to `int`, but this tool
    // sees the tree before that, when it is still 64 bits. A long that comes from a system typedef (ssize_t,
    // intptr_t, time_t) stays 64 bits.
    bool IsEngineLong(QualType type, bool wantUnsigned) const
    {
        if (type.isNull())
            return false;
        for (;;)
        {
            if (const auto *td = type->getAs<TypedefType>())
            {
                if (m_sm.isInSystemHeader(td->getDecl()->getLocation()))
                    return false;
                type = td->getDecl()->getUnderlyingType();
                continue;
            }
            break;
        }
        const auto *builtin = type.getCanonicalType()->getAs<BuiltinType>();
        return builtin && builtin->getKind() == (wantUnsigned ? BuiltinType::ULong : BuiltinType::Long);
    }
    bool FromSystemTypedef(QualType type) const
    {
        while (const auto *td = type->getAs<TypedefType>())
        {
            if (m_sm.isInSystemHeader(td->getDecl()->getLocation()))
                return true;
            type = td->getDecl()->getUnderlyingType();
        }
        return false;
    }
    // the width the value will have in the rewritten tree
    unsigned RewrittenWidth(QualType type) const
    {
        if (IsEngineLong(type, false) || IsEngineLong(type, true))
            return 32;
        return (unsigned)m_context.getTypeSize(type);
    }

    bool IsUnsigned32(const Expr *e) const
    {
        if (!e)
            return false;
        const QualType type = e->getType();
        if (type.isNull() || type->isDependentType() || !type->isUnsignedIntegerType() || type->isBooleanType())
            return false;
        if (RewrittenWidth(type) != 32)
            return false;
        return !isa<IntegerLiteral>(e->IgnoreParenImpCasts());
    }
    bool VisitArraySubscriptExpr(ArraySubscriptExpr *sub)
    {
        if (IsUnsigned32(sub->getIdx()))
            Wrap('I', sub->getIdx()->getSourceRange());
        return true;
    }
    bool VisitBinaryOperator(BinaryOperator *op)
    {
        const BinaryOperatorKind kind = op->getOpcode();
        if (kind != BO_Add && kind != BO_Sub && kind != BO_AddAssign && kind != BO_SubAssign)
            return true;
        const Expr *lhs = op->getLHS(), *rhs = op->getRHS();
        if (lhs->getType().isNull() || rhs->getType().isNull())
            return true;
        if (lhs->getType()->isPointerType() && IsUnsigned32(rhs))
            Wrap('I', rhs->getSourceRange());
        else if (kind == BO_Add && rhs->getType()->isPointerType() && IsUnsigned32(lhs))
            Wrap('I', lhs->getSourceRange());
        return true;
    }

    // const_cast cannot cross address spaces: widen the operand first
    bool VisitCXXConstCastExpr(CXXConstCastExpr *cast)
    {
        const Expr *sub = cast->getSubExprAsWritten();
        // (an array operand decays to a pointer in the 32-bit address space too)
        if (sub && !sub->getType().isNull() && !sub->getType()->isDependentType() &&
            (sub->getType()->isPointerType() || sub->getType()->isArrayType()) && !sub->getType()->isFunctionPointerType())
            Wrap('D', sub->getSourceRange());
        return true;
    }

    // ... and so is the outermost pointer of a return type
    bool VisitFunctionTypeLoc(FunctionTypeLoc loc)
    {
        ExcludeTopLevel(loc.getReturnLoc());
        return true;
    }

    // a cast's result is a value too: `*(T *)p` must be an ordinary lvalue
    bool VisitExplicitCastExpr(ExplicitCastExpr *cast)
    {
        if (TypeSourceInfo *tsi = cast->getTypeInfoAsWritten())
            ExcludeTopLevel(tsi->getTypeLoc());
        // (Derived *)base: clang keeps the result of a downcast of a 32-bit pointer 32 bits wide when a member's
        // address is then taken through it (`&((Derived *)p)->member` came out with an undefined upper half).
        // The operand is widened first.
        if (cast->getCastKind() == CK_BaseToDerived)
        {
            const Expr *sub = cast->getSubExprAsWritten();
            if (sub && !sub->getType().isNull() && !sub->getType()->isDependentType() && sub->getType()->isPointerType())
                Wrap('B', sub->getSourceRange());
        }
        // (T *)someInt: decompiled code keeps addresses in `int` variables and fields. A signed 32-bit value
        // sign-extends to a 64-bit pointer, which is wrong for every address from 2 GB up: go through unsigned.
        if (cast->getCastKind() == CK_IntegralToPointer)
        {
            const Expr *sub = cast->getSubExprAsWritten();
            const QualType type = sub ? sub->getType() : QualType();
            // Also a 64-bit integer of the engine's own (`__int64 args` carrying two packed 32-bit pointers, cast
            // with `(const char **)args`): 32-bit code kept the low half. Host-sized integers from system typedefs
            // (uintptr_t, size_t) are left alone.
            if (!type.isNull() && !type->isDependentType() && type->isIntegralOrEnumerationType() &&
                !isa<IntegerLiteral>(sub->IgnoreParenImpCasts()))
            {
                const unsigned width = RewrittenWidth(type);
                const bool narrowSigned = type->isSignedIntegerOrEnumerationType() && width <= 32;
                const bool wideEngine = width == 64 && !FromSystemTypedef(type);
                if (narrowSigned || wideEngine)
                    Wrap('U', sub->getSourceRange());
            }
        }
        return true;
    }

    // Declarations whose parenthesized declarators clang parses without trouble: `void (*P field)(int)`.
    // Everywhere else (parameters, casts, sizeof, template arguments) `(* __ptr32` makes clang's
    // declaration-or-expression guess fail ("expected expression"), so stars written right after a '(' are
    // only annotated when they belong to one of these.
    bool VisitFieldDecl(FieldDecl *decl)
    {
        AllowParenStars(decl->getTypeSourceInfo());
        return true;
    }
    bool VisitVarDecl(VarDecl *decl)
    {
        // not block-scope variables: `float (* __ptr32 p)[4];` in a function body is taken for an expression
        if (!isa<ParmVarDecl>(decl) && !decl->isLocalVarDecl())
            AllowParenStars(decl->getTypeSourceInfo());
        if (decl->isLocalVarDecl() && !decl->hasGlobalStorage() && InDirs(decl->getLocation(), s_localsNative))
            if (TypeSourceInfo *tsi = decl->getTypeSourceInfo())
                ExcludeTopLevel(tsi->getTypeLoc());
        // Static storage, and automatic aggregates: clang copies `T *names[] = {"a", "b"}` in a function from
        // a private constant, which has the same problem.
        if (decl->hasInit() && !decl->isConstexpr())
        {
            const Expr *init = decl->getInit();
            if (const auto *cleanups = dyn_cast<ExprWithCleanups>(init))
                init = cleanups->getSubExpr();
            if (decl->hasGlobalStorage() || isa<InitListExpr>(init))
                WrapAddresses(init);
        }
        return true;
    }
    bool VisitTypedefNameDecl(TypedefNameDecl *decl)
    {
        AllowParenStars(decl->getTypeSourceInfo());
        return true;
    }

    // main(int, char **argv): the C runtime's own array of native pointers
    bool TraverseFunctionDecl(FunctionDecl *decl)
    {
        if (decl->isMain())
        {
            for (ParmVarDecl *p : decl->parameters())
                if (TypeSourceInfo *tsi = p->getTypeSourceInfo())
                    ExcludeAll(tsi->getTypeLoc());
        }
        return RecursiveASTVisitor<Visitor>::TraverseFunctionDecl(decl);
    }

    bool VisitPointerTypeLoc(PointerTypeLoc loc)
    {
        // clang 21 crashes (Sema, template instantiation) on `__ptr32 __uptr` pointers whose dependent pointee is
        // itself a pointer or is written as a template specialization: `T * P * P`, `V<T> * P`. A template
        // parameter, a nested class or a typedef as the pointee is fine (`T * P`, `Node * P`, `typedef V<T> VT;
        // VT * P`). Such a star is left native and reported ("W"): give the pointee a member typedef in the
        // source.
        QualType pointee = loc.getPointeeLoc().getType();
        if (!pointee.isNull() && pointee->isDependentType())
        {
            const Type *written = pointee.getTypePtr();
            while (const auto *elaborated = dyn_cast<ElaboratedType>(written))
                written = elaborated->getNamedType().getTypePtr();
            while (const auto *paren = dyn_cast<ParenType>(written))
                written = paren->getInnerType().getTypePtr();
            if (isa<PointerType>(written) || isa<AttributedType>(written) || isa<TemplateSpecializationType>(written) ||
                isa<DependentTemplateSpecializationType>(written))
            {
                const SourceLocation star = loc.getStarLoc();
                if (!star.isMacroID() && Rewritable(star))
                    printf("W\t%s\t%u\n", PathOf(star).c_str(), m_sm.getSpellingLineNumber(star));
                return true;
            }
        }
        m_stars.push_back(loc.getStarLoc());
        return true;
    }

    // long / unsigned long: every `long` token of the written specifiers
    bool VisitBuiltinTypeLoc(BuiltinTypeLoc loc)
    {
        const BuiltinType::Kind kind = loc.getTypePtr()->getKind();
        // long double is a double to this code (MSVC). x86 has -mlong-double-64 for that; AArch64 has no such
        // option (its long double is a 128-bit software type), so the `long` goes away in the text.
        const bool longDouble = kind == BuiltinType::LongDouble;
        if (kind != BuiltinType::Long && kind != BuiltinType::ULong && !longDouble)
            return true;
        SourceRange range = loc.getSourceRange();
        if (range.isInvalid())
            return true;
        if (range.getBegin().isMacroID() || range.getEnd().isMacroID())
        {
            // written inside a macro argument (zlib's OF((unsigned long crc, ...))): rewrite it there. Spelled by
            // a macro body: left for a compile error or a manual fix.
            if (!m_sm.isMacroArgExpansion(range.getBegin()) || !m_sm.isMacroArgExpansion(range.getEnd()))
                return true;
            range = SourceRange(m_sm.getSpellingLoc(range.getBegin()), m_sm.getSpellingLoc(range.getEnd()));
        }
        if (!Rewritable(range.getBegin()) && (longDouble || !LongOnly(range.getBegin())))
            return true;
        const LangOptions &lo = m_context.getLangOpts();
        SourceLocation at = range.getBegin();
        const SourceLocation end = Lexer::getLocForEndOfToken(range.getEnd(), 0, m_sm, lo);
        for (int guard = 0; guard < 8 && at.isValid() && m_sm.isBeforeInTranslationUnit(at, end); ++guard)
        {
            Token tok;
            if (Lexer::getRawToken(at, tok, m_sm, lo, true))
                break;
            if (tok.is(tok::raw_identifier) && tok.getRawIdentifier() == "long")
                printf("%s\t%s\t%u\n", longDouble ? "F" : "L", PathOf(tok.getLocation()).c_str(), m_sm.getFileOffset(tok.getLocation()));
            at = tok.getEndLoc();
        }
        return true;
    }

    // a call through a function pointer object: widen the callee first
    bool VisitCallExpr(CallExpr *call)
    {
        WrapVariadicPointers(call);
        if (isa<CXXMemberCallExpr>(call) || isa<CXXOperatorCallExpr>(call) || isa<UserDefinedLiteral>(call))
            return true;
        const Expr *callee = call->getCallee();
        if (!callee)
            return true;
        const Expr *bare = callee->IgnoreParenImpCasts();
        if (isa<DeclRefExpr>(bare) && isa<FunctionDecl>(cast<DeclRefExpr>(bare)->getDecl()))
            return true;   // a direct call
        if (isa<UnresolvedLookupExpr>(bare) || isa<UnresolvedMemberExpr>(bare) || isa<CXXDependentScopeMemberExpr>(bare) ||
            isa<CXXPseudoDestructorExpr>(bare) || isa<DependentScopeDeclRefExpr>(bare) || isa<BuiltinType>(bare->getType()))
            return true;
        if (const auto *member = dyn_cast<MemberExpr>(bare))
            if (isa<CXXMethodDecl>(member->getMemberDecl()))
                return true;
        const QualType type = bare->getType();
        if (type.isNull() || type->isDependentType())
            return true;
        if (!type->isFunctionPointerType())
            return true;   // a lambda, a functor, a function reference
        WrapNative(callee->getSourceRange());
        return true;
    }

    // A pointer passed through `...` is read back as 8 bytes (printf's %s, va_arg), but an argument that is a
    // 32-bit pointer value is passed as 4: the other half of the register or stack slot is whatever was there
    // (seen on AArch64 as va("%s", dvar->name) faulting on 0x20'80ffc1a0). Every such argument is widened.
    void WrapVariadicPointers(const CallExpr *call)
    {
        if (isa<CXXOperatorCallExpr>(call) || isa<UserDefinedLiteral>(call) || !call->getCallee())
            return;
        QualType type = call->getCallee()->getType();
        if (type.isNull() || type->isDependentType())
            return;
        if (const PointerType *pointer = type->getAs<PointerType>())
            type = pointer->getPointeeType();
        const FunctionProtoType *proto = type->getAs<FunctionProtoType>();
        if (!proto)
            if (const FunctionDecl *direct = call->getDirectCallee())
                proto = direct->getType()->getAs<FunctionProtoType>();
        if (!proto || !proto->isVariadic())
            return;
        for (unsigned i = proto->getNumParams(); i < call->getNumArgs(); ++i)
        {
            const Expr *arg = call->getArg(i);
            if (!arg || arg->getType().isNull() || arg->getType()->isDependentType() || !arg->getType()->isPointerType())
                continue;
            if (isa<StringLiteral>(arg->IgnoreParenImpCasts()) || isa<CXXDefaultArgExpr>(arg))
                continue;
            Wrap('V', arg->getSourceRange());
        }
    }

    // delete of a 32-bit pointer object: operator delete wants the native pointer
    bool VisitCXXDeleteExpr(CXXDeleteExpr *del)
    {
        if (const Expr *arg = del->getArgument())
            if (!arg->getType().isNull() && !arg->getType()->isDependentType())
                Wrap('D', arg->getSourceRange());
        return true;
    }

    void Emit()
    {
        for (const ParamStar &ps : m_paramStars)
            if (m_addressTaken.count({ps.function, ps.index}) && !ps.nativeDir)
                m_excluded.erase(ps.star);
        for (SourceLocation star : m_stars)
        {
            if (star.isInvalid() || m_excluded.count(star.getRawEncoding()))
                continue;
            // a star that came out of a macro: annotate where it is written (macro body or macro argument)
            SourceLocation at = star.isMacroID() ? m_sm.getSpellingLoc(star) : star;
            if (at.isInvalid() || m_sm.isInSystemHeader(at) || m_sm.isWrittenInScratchSpace(at))
                continue;
            const FileID fid = m_sm.getFileID(at);
            OptionalFileEntryRef file = m_sm.getFileEntryRefForID(fid);
            if (!file)
                continue;
            const std::string path = PathOf(at);
            if (!InRoot(path))
                continue;
            const unsigned offset = m_sm.getFileOffset(at);
            bool invalid = false;
            const llvm::StringRef text = m_sm.getBufferData(fid, &invalid);
            if (invalid || offset >= text.size() || text[offset] != '*')
                continue;
            if (AfterOpenParen(text, offset) && !m_parenOk.count(star.getRawEncoding()))
            {
                printf("W\t%s\t%u\n", path.c_str(), m_sm.getSpellingLineNumber(at));
                continue;
            }
            printf("P\t%s\t%u\n", path.c_str(), offset);
        }
    }

private:
    std::string PathOf(SourceLocation at) const
    {
        OptionalFileEntryRef file = m_sm.getFileEntryRefForID(m_sm.getFileID(at));
        if (!file)
            return std::string();
        llvm::SmallString<512> path(file->getName());
        m_sm.getFileManager().makeAbsolutePath(path);
        llvm::sys::path::remove_dots(path, true);
        return std::string(path.str());
    }

    // written directly (not through a macro) in a file this tool rewrites
    bool Rewritable(SourceLocation at) const
    {
        if (at.isInvalid() || at.isMacroID() || m_sm.isInSystemHeader(at))
            return false;
        const std::string path = PathOf(at);
        return !path.empty() && InRoot(path);
    }

    // Static initializers: every address stored into a pointer goes through BO1_ADDR (native_p32.h).
    void WrapAddresses(const Expr *init)
    {
        if (!init)
            return;
        if (const auto *cleanups = dyn_cast<ExprWithCleanups>(init))
            init = cleanups->getSubExpr();
        if (const auto *list = dyn_cast<InitListExpr>(init))
        {
            // the syntactic form holds what is written; the semantic form has the conversions (types)
            const InitListExpr *semantic = list->isSemanticForm() ? list : list->getSemanticForm();
            if (!semantic)
                semantic = list;
            for (const Expr *child : semantic->inits())
                WrapAddresses(child);
            return;
        }
        if (isa<ImplicitValueInitExpr>(init) || isa<DesignatedInitExpr>(init) || isa<CXXConstructExpr>(init) ||
            isa<ArrayInitLoopExpr>(init) || isa<NoInitExpr>(init))
            return;
        const QualType type = init->getType();
        if (type.isNull() || type->isDependentType() || !type->isPointerType())
            return;
        const Expr *bare = init->IgnoreParenImpCasts();
        if (init->isNullPointerConstant(m_context, Expr::NPC_ValueDependentIsNotNull) ||
            isa<CXXNullPtrLiteralExpr>(bare) || isa<GNUNullExpr>(bare) || isa<IntegerLiteral>(bare))
            return;
        // the expression as written (without the implicit conversion to the element type)
        const SourceRange range = init->getSourceRange();
        if (range.isValid())
            Wrap('A', range);
    }

    bool InDirs(SourceLocation at, const llvm::cl::list<std::string> &dirs) const
    {
        if (at.isInvalid())
            return false;
        if (at.isMacroID())
            at = m_sm.getExpansionLoc(at);
        const std::string pathString = PathOf(at);
        llvm::StringRef path(pathString), root(s_root);
        if (!path.starts_with(root))
            return false;
        llvm::StringRef rel = path.substr(root.size());
        while (rel.starts_with("/"))
            rel = rel.substr(1);
        for (const std::string &dir : dirs)
            if (rel.starts_with(dir) && rel.size() > dir.size() && rel[dir.size()] == '/')
                return true;
        return false;
    }

    bool LongOnly(SourceLocation at) const { return !at.isMacroID() && InDirs(at, s_longOnly); }

    void WrapNative(SourceRange range) { Wrap('N', range); }

    void Wrap(char kind, SourceRange range)
    {
        SourceLocation begin = range.getBegin(), last = range.getEnd();
        if (begin.isMacroID() || last.isMacroID())
        {
            const LangOptions &lo = m_context.getLangOpts();
            SourceLocation macroBegin, macroEnd;
            if (m_sm.isMacroArgExpansion(begin) && m_sm.isMacroArgExpansion(last))
            {
                // an expression that is entirely one macro argument is rewritten where the argument is written
                begin = m_sm.getSpellingLoc(begin);
                last = m_sm.getSpellingLoc(last);
            }
            else if (begin.isMacroID() && last.isMacroID() && Lexer::isAtStartOfMacroExpansion(begin, m_sm, lo, &macroBegin) &&
                     Lexer::isAtEndOfMacroExpansion(last, m_sm, lo, &macroEnd) && macroBegin.isFileID() && macroEnd.isFileID())
            {
                // an expression that is exactly one macro invocation (`_InterlockedCompareExchange(...)`): wrap the
                // invocation. isAtEndOfMacroExpansion gives the location after its last token.
                const unsigned b = m_sm.getFileOffset(macroBegin), e = m_sm.getFileOffset(macroEnd);
                if (Rewritable(macroBegin) && m_sm.getFileID(macroBegin) == m_sm.getFileID(macroEnd) && e > b)
                    printf("%c\t%s\t%u\t%u\n", kind, PathOf(macroBegin).c_str(), b, e);
                return;
            }
            else
            {
                return;
            }
        }
        if (!Rewritable(begin) || !Rewritable(last) || m_sm.getFileID(begin) != m_sm.getFileID(last))
            return;
        const SourceLocation end = Lexer::getLocForEndOfToken(last, 0, m_sm, m_context.getLangOpts());
        if (end.isInvalid())
            return;
        const unsigned b = m_sm.getFileOffset(begin), e = m_sm.getFileOffset(end);
        if (e > b)
            printf("%c\t%s\t%u\t%u\n", kind, PathOf(begin).c_str(), b, e);
    }

    static TypeLoc Strip(TypeLoc loc)
    {
        for (;;)
        {
            if (loc.isNull())
                return loc;
            if (auto q = loc.getAs<QualifiedTypeLoc>())
                loc = q.getUnqualifiedLoc();
            else if (auto p = loc.getAs<ParenTypeLoc>())
                loc = p.getInnerLoc();
            else if (auto a = loc.getAs<AttributedTypeLoc>())
                loc = a.getModifiedLoc();
            else if (auto m = loc.getAs<MacroQualifiedTypeLoc>())
                loc = m.getInnerLoc();
            else
                return loc;
        }
    }

    void ExcludeTopLevel(TypeLoc loc)
    {
        loc = Strip(loc);
        if (loc.isNull())
            return;
        if (auto p = loc.getAs<PointerTypeLoc>())
            m_excluded.insert(p.getStarLoc().getRawEncoding());
    }

    // the stars of the outermost declarator: everything down to the first parenthesis
    void AllowParenStars(TypeSourceInfo *tsi)
    {
        if (!tsi)
            return;
        for (TypeLoc loc = tsi->getTypeLoc(); !loc.isNull(); loc = loc.getNextTypeLoc())
        {
            if (auto p = loc.getAs<PointerTypeLoc>())
                m_parenOk.insert(p.getStarLoc().getRawEncoding());
            else if (loc.getAs<FunctionTypeLoc>())
                break;   // below: return and parameter types, which are other declarators
        }
    }

    // is this '*' the first thing in a parenthesized declarator: "(*", "(__cdecl *", "(**"? Only whitespace, other
    // stars and calling-convention keywords may stand between the '(' and it.
    static bool AfterOpenParen(llvm::StringRef text, unsigned offset)
    {
        static const char *const conventions[] = {"__cdecl", "__stdcall", "__fastcall", "__thiscall", "__vectorcall",
            "_cdecl", "_stdcall", "_fastcall", "WINAPI", "CALLBACK", "APIENTRY", "STDMETHODCALLTYPE", "S_CALLTYPE",
            "QDECL", "BO1_P32"};
        unsigned i = offset;
        while (i > 0)
        {
            const char c = text[i - 1];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '*')
            {
                --i;
                continue;
            }
            if (c == '(')
                return true;
            // an identifier: fine if it is a calling convention, otherwise this is `type *`
            unsigned b = i;
            while (b > 0 && (text[b - 1] == '_' || isalnum((unsigned char)text[b - 1])))
                --b;
            if (b == i)
                return false;
            const llvm::StringRef word = text.substr(b, i - b);
            bool known = false;
            for (const char *k : conventions)
                known = known || word == k;
            if (!known)
                return false;
            i = b;
        }
        return false;
    }

    void ExcludeAll(TypeLoc loc)
    {
        for (loc = Strip(loc); !loc.isNull(); loc = Strip(loc.getNextTypeLoc()))
        {
            if (auto p = loc.getAs<PointerTypeLoc>())
                m_excluded.insert(p.getStarLoc().getRawEncoding());
        }
    }

    bool InRoot(const std::string &pathString) const
    {
        llvm::StringRef path(pathString);
        llvm::StringRef root(s_root);
        if (!path.starts_with(root))
            return false;
        llvm::StringRef rel = path.substr(root.size());
        while (rel.starts_with("/"))
            rel = rel.substr(1);
        for (const std::string &dir : s_exclude)
        {
            llvm::StringRef d(dir);
            if (rel.starts_with(d) && (rel.size() == d.size() || rel[d.size()] == '/' || d.ends_with("/")))
                return false;
        }
        return true;
    }

    ASTContext &m_context;
    SourceManager &m_sm;
    std::vector<SourceLocation> m_stars;
    std::set<unsigned> m_excluded;
    std::set<unsigned> m_parenOk;
    struct ParamStar
    {
        const FunctionDecl *function;
        unsigned index;
        unsigned star;
        bool nativeDir;   // platform code: its parameters stay native even when their address is taken
    };
    std::vector<ParamStar> m_paramStars;
    std::set<std::pair<const FunctionDecl *, unsigned>> m_addressTaken;
};

class Consumer : public ASTConsumer
{
public:
    void HandleTranslationUnit(ASTContext &context) override
    {
        Visitor visitor(context);
        visitor.TraverseDecl(context.getTranslationUnitDecl());
        visitor.Emit();
        fflush(stdout);
    }
};

class Action : public ASTFrontendAction
{
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override
    {
        return std::make_unique<Consumer>();
    }
};
}

int main(int argc, const char **argv)
{
    auto parser = tooling::CommonOptionsParser::create(argc, argv, s_category);
    if (!parser)
    {
        llvm::errs() << llvm::toString(parser.takeError()) << "\n";
        return 2;
    }
    tooling::ClangTool tool(parser->getCompilations(), parser->getSourcePathList());
    // the sources still fail their 32-bit layout checks when parsed as 64-bit code: that is what this fixes
    tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster({"-ferror-limit=0", "-w", "-Wno-everything"},
        tooling::ArgumentInsertPosition::END));
    tool.run(tooling::newFrontendActionFactory<Action>().get());
    return 0;   // parse errors are expected (see above); the insertions found are still valid
}
