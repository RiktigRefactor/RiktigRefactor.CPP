#include "Independence.h"

#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace riktig {

using namespace clang;

namespace {

const Stmt *parentIgnoringParens(ASTContext &context, const Stmt *statement) {
    auto parents = context.getParents(*statement);
    while (parents.size() == 1) {
        const auto *parent = parents[0].get<Stmt>();
        if (!parent || !isa<ParenExpr>(parent)) {
            return parent;
        }
        parents = context.getParents(*parent);
    }
    return nullptr;
}

// A use that only reads the variable's value, or only assigns or increments it.
bool isPlainReadOrWrite(ASTContext &context, const DeclRefExpr *reference) {
    const auto *parent = parentIgnoringParens(context, reference);
    if (const auto *cast = dyn_cast_or_null<ImplicitCastExpr>(parent);
        cast && cast->getCastKind() == CK_LValueToRValue) {
        return true;
    }
    if (const auto *binary = dyn_cast_or_null<BinaryOperator>(parent);
        binary && binary->isAssignmentOp() && binary->getLHS()->IgnoreParens() == reference) {
        return true;
    }
    if (const auto *unary = dyn_cast_or_null<UnaryOperator>(parent); unary && unary->isIncrementDecrementOp()) {
        return true;
    }
    return false;
}

// Variables whose address is taken, that a reference binds to, or that a lambda captures by reference: code
// anywhere could change them through the alias.
class AliasFinder : public RecursiveASTVisitor<AliasFinder> {
  public:
    explicit AliasFinder(ASTContext &context, std::set<const VarDecl *> &aliased)
        : context_(context), aliased_(aliased) {}

    bool VisitDeclRefExpr(DeclRefExpr *reference) {
        if (const auto *variable = dyn_cast<VarDecl>(reference->getDecl())) {
            if (!isPlainReadOrWrite(context_, reference)) {
                aliased_.insert(variable);
            }
        }
        return true;
    }

    bool VisitLambdaExpr(LambdaExpr *lambda) {
        for (const auto &capture : lambda->captures()) {
            if (capture.capturesVariable() && capture.getCaptureKind() == LCK_ByRef) {
                if (const auto *variable = dyn_cast<VarDecl>(capture.getCapturedVar())) {
                    aliased_.insert(variable);
                }
            }
        }
        return true;
    }

  private:
    ASTContext &context_;
    std::set<const VarDecl *> &aliased_;
};

class WriteFinder : public RecursiveASTVisitor<WriteFinder> {
  public:
    bool VisitBinaryOperator(BinaryOperator *binary) {
        if (binary->isAssignmentOp()) {
            note(binary->getLHS());
        }
        return true;
    }
    bool VisitUnaryOperator(UnaryOperator *unary) {
        if (unary->isIncrementDecrementOp()) {
            note(unary->getSubExpr());
        }
        return true;
    }
    std::set<const VarDecl *> written;

  private:
    void note(const Expr *target) {
        if (const auto *reference = dyn_cast<DeclRefExpr>(target->IgnoreParens())) {
            if (const auto *variable = dyn_cast<VarDecl>(reference->getDecl())) {
                written.insert(variable);
            }
        }
    }
};

} // namespace

IndependenceAnalysis::IndependenceAnalysis(ASTContext &context, const FunctionDecl *enclosingFunction)
    : context_(context), enclosingFunction_(enclosingFunction) {
    if (enclosingFunction_ && enclosingFunction_->hasBody()) {
        AliasFinder finder(context_, aliased_);
        finder.TraverseDecl(const_cast<FunctionDecl *>(enclosingFunction_));
    }
}

bool IndependenceAnalysis::isUnaliasedLocal(const VarDecl *variable) const {
    if (!enclosingFunction_ || !variable->hasLocalStorage() || variable->getType()->isReferenceType() ||
        variable->getType().isVolatileQualified() || aliased_.count(variable)) {
        return false;
    }
    for (const DeclContext *context = variable->getDeclContext(); context; context = context->getParent()) {
        if (context == enclosingFunction_) {
            return true;
        }
    }
    return false;
}

bool IndependenceAnalysis::isEffectFreeGlvalue(const Expr *expression, std::set<const VarDecl *> &reads) const {
    expression = expression->IgnoreParens();
    if (const auto *cast = dyn_cast<ImplicitCastExpr>(expression)) {
        auto kind = cast->getCastKind();
        if (kind == CK_NoOp || kind == CK_DerivedToBase || kind == CK_UncheckedDerivedToBase) {
            return isEffectFreeGlvalue(cast->getSubExpr(), reads);
        }
        return false;
    }
    if (const auto *reference = dyn_cast<DeclRefExpr>(expression)) {
        // Naming a variable designates it without reading it.
        return isa<VarDecl>(reference->getDecl()) && !cast<VarDecl>(reference->getDecl())->getType()->isReferenceType();
    }
    if (const auto *unary = dyn_cast<UnaryOperator>(expression); unary && unary->getOpcode() == UO_Deref) {
        return isEffectFreeValue(unary->getSubExpr(), reads);
    }
    if (const auto *member = dyn_cast<MemberExpr>(expression); member && isa<FieldDecl>(member->getMemberDecl())) {
        if (member->isArrow()) {
            return isEffectFreeValue(member->getBase(), reads);
        }
        return isEffectFreeGlvalue(member->getBase(), reads);
    }
    return false;
}

bool IndependenceAnalysis::isEffectFreeValue(const Expr *expression, std::set<const VarDecl *> &reads) const {
    expression = expression->IgnoreParens();
    if (isa<IntegerLiteral, FloatingLiteral, CharacterLiteral, CXXBoolLiteralExpr, CXXNullPtrLiteralExpr, StringLiteral,
            CXXThisExpr>(expression)) {
        return true;
    }
    if (const auto *cast = dyn_cast<ImplicitCastExpr>(expression)) {
        switch (cast->getCastKind()) {
        case CK_LValueToRValue: {
            const auto *reference = dyn_cast<DeclRefExpr>(cast->getSubExpr()->IgnoreParens());
            const auto *variable = reference ? dyn_cast<VarDecl>(reference->getDecl()) : nullptr;
            if (!variable || !variable->getType()->isScalarType()) {
                return false;
            }
            if (variable->isConstexpr()) {
                return true;
            }
            if (!isUnaliasedLocal(variable)) {
                return false;
            }
            reads.insert(variable);
            return true;
        }
        case CK_UserDefinedConversion:
        case CK_ConstructorConversion:
            return false;
        case CK_ArrayToPointerDecay:
        case CK_FunctionToPointerDecay:
            return isEffectFreeGlvalue(cast->getSubExpr(), reads) ||
                   isa<DeclRefExpr>(cast->getSubExpr()->IgnoreParens());
        default:
            return cast->getSubExpr()->isPRValue() && isEffectFreeValue(cast->getSubExpr(), reads);
        }
    }
    if (const auto *reference = dyn_cast<DeclRefExpr>(expression)) {
        return isa<EnumConstantDecl>(reference->getDecl());
    }
    if (const auto *unary = dyn_cast<UnaryOperator>(expression)) {
        switch (unary->getOpcode()) {
        case UO_Plus:
        case UO_Minus:
        case UO_Not:
        case UO_LNot:
            return unary->getSubExpr()->getType()->isScalarType() && isEffectFreeValue(unary->getSubExpr(), reads);
        case UO_AddrOf:
            return isEffectFreeGlvalue(unary->getSubExpr(), reads);
        default:
            return false;
        }
    }
    if (const auto *binary = dyn_cast<BinaryOperator>(expression)) {
        if (binary->isAssignmentOp() || binary->getOpcode() == BO_Comma || binary->isPtrMemOp()) {
            return false;
        }
        return isEffectFreeValue(binary->getLHS(), reads) && isEffectFreeValue(binary->getRHS(), reads);
    }
    if (const auto *conditional = dyn_cast<ConditionalOperator>(expression)) {
        return conditional->isPRValue() && isEffectFreeValue(conditional->getCond(), reads) &&
               isEffectFreeValue(conditional->getTrueExpr(), reads) &&
               isEffectFreeValue(conditional->getFalseExpr(), reads);
    }
    return false;
}

IndependenceAnalysis::Facts IndependenceAnalysis::factsOf(const Expr *expression) const {
    Facts facts;
    std::set<const VarDecl *> reads;
    bool effectFree =
        expression->isGLValue() ? isEffectFreeGlvalue(expression, reads) : isEffectFreeValue(expression, reads);
    // An argument initializing a reference parameter is a glvalue; one copied into a class parameter is a
    // construction, which may run code.
    facts.readsOnlyUnaliasedLocals = effectFree;
    facts.readLocals = reads;
    WriteFinder writes;
    writes.TraverseStmt(const_cast<Expr *>(expression));
    facts.writtenLocals = writes.written;
    return facts;
}

static bool disjoint(const std::set<const VarDecl *> &first, const std::set<const VarDecl *> &second) {
    for (const auto *variable : first) {
        if (second.count(variable)) {
            return false;
        }
    }
    return true;
}

bool IndependenceAnalysis::areIndependent(const Expr *first, const Expr *second) const {
    auto firstFacts = factsOf(first);
    auto secondFacts = factsOf(second);
    return (firstFacts.readsOnlyUnaliasedLocals && disjoint(firstFacts.readLocals, secondFacts.writtenLocals)) ||
           (secondFacts.readsOnlyUnaliasedLocals && disjoint(secondFacts.readLocals, firstFacts.writtenLocals));
}

} // namespace riktig
