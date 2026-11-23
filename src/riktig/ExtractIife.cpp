#include "ExtractIife.h"

#include "RawTokens.h"
#include "RefusalCodes.h"
#include "SourceText.h"
#include "Verification.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

namespace riktig {

using namespace clang;
namespace codes = refusal_codes;

namespace {

struct OffsetRange {
    unsigned begin = 0;
    unsigned end = 0;
};

std::optional<OffsetRange> offsetRangeOf(const SourceManager &sourceManager, const LangOptions &options,
                                         SourceRange range, const std::string &file) {
    if (range.getBegin().isMacroID() || range.getEnd().isMacroID()) {
        return std::nullopt;
    }
    auto begin = fileOffsetOf(sourceManager, range.getBegin());
    auto end = fileOffsetOf(sourceManager, Lexer::getLocForEndOfToken(range.getEnd(), 0, sourceManager, options));
    if (!begin || !end || begin->file != file || end->file != file) {
        return std::nullopt;
    }
    return OffsetRange{begin->offset, end->offset};
}

// A statement's range, including the `;` that ends an expression statement.
std::optional<OffsetRange> statementRangeOf(const SourceManager &sourceManager, const LangOptions &options,
                                            const Stmt *statement, const std::string &file) {
    auto range = offsetRangeOf(sourceManager, options, statement->getSourceRange(), file);
    if (!range) {
        return std::nullopt;
    }
    auto afterSemicolon = Lexer::findLocationAfterToken(statement->getEndLoc(), tok::semi, sourceManager, options,
                                                        /*SkipTrailingWhitespaceAndNewLine=*/false);
    if (afterSemicolon.isValid() && !isa<DeclStmt>(statement)) {
        if (auto end = fileOffsetOf(sourceManager, afterSemicolon); end && end->file == file) {
            range->end = end->offset;
        }
    }
    return range;
}

bool isImplicitWrapper(const Expr *expression) {
    if (isa<ImplicitCastExpr, FullExpr, MaterializeTemporaryExpr, CXXBindTemporaryExpr, CXXDefaultArgExpr,
            OpaqueValueExpr>(expression)) {
        return true;
    }
    if (const auto *construction = dyn_cast<CXXConstructExpr>(expression)) {
        return !isa<CXXTemporaryObjectExpr>(construction) && construction->getParenOrBraceRange().isInvalid() &&
               !construction->isListInitialization();
    }
    return false;
}

class SelectionFinder : public RecursiveASTVisitor<SelectionFinder> {
  public:
    SelectionFinder(ASTContext &context, const std::string &file, unsigned start, unsigned end)
        : sourceManager_(context.getSourceManager()), options_(context.getLangOpts()), file_(file), start_(start),
          end_(end) {}

    bool VisitStmt(Stmt *statement) {
        if (const auto *expression = dyn_cast<Expr>(statement); expression && !expression_ && !block_) {
            if (!isImplicitWrapper(expression)) {
                auto range = offsetRangeOf(sourceManager_, options_, expression->getSourceRange(), file_);
                if (range && range->begin == start_ && range->end == end_) {
                    expression_ = expression;
                }
            }
        }
        if (const auto *block = dyn_cast<CompoundStmt>(statement); block && !block_) {
            findStatements(block);
        }
        return true;
    }

    const Expr *expression_ = nullptr;
    const CompoundStmt *block_ = nullptr;
    std::vector<const Stmt *> statements_;
    bool endsBlock_ = false;

  private:
    void findStatements(const CompoundStmt *block) {
        std::vector<const Stmt *> selected;
        bool started = false;
        const Stmt *last = block->body_empty() ? nullptr : block->body_back();
        for (const auto *child : block->body()) {
            auto range = statementRangeOf(sourceManager_, options_, child, file_);
            if (!range) {
                if (started) {
                    return;
                }
                continue;
            }
            if (range->begin == start_) {
                started = true;
            }
            if (started) {
                selected.push_back(child);
                if (range->end == end_) {
                    block_ = block;
                    statements_ = selected;
                    endsBlock_ = child == last;
                    return;
                }
                if (range->end > end_) {
                    return;
                }
            }
        }
    }

    const SourceManager &sourceManager_;
    const LangOptions &options_;
    const std::string &file_;
    unsigned start_;
    unsigned end_;
};

const Expr *ignoringParens(const Expr *expression) { return expression->IgnoreParens(); }

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

enum class VariableUse { Read, Write, Other };

VariableUse classifyUse(ASTContext &context, const DeclRefExpr *reference) {
    const auto *parent = parentIgnoringParens(context, reference);
    if (const auto *cast = dyn_cast_or_null<ImplicitCastExpr>(parent);
        cast && cast->getCastKind() == CK_LValueToRValue) {
        return VariableUse::Read;
    }
    if (const auto *binary = dyn_cast_or_null<BinaryOperator>(parent);
        binary && binary->isAssignmentOp() && ignoringParens(binary->getLHS()) == reference) {
        return VariableUse::Write;
    }
    if (const auto *unary = dyn_cast_or_null<UnaryOperator>(parent); unary && unary->isIncrementDecrementOp()) {
        return VariableUse::Write;
    }
    return VariableUse::Other;
}

// Variables whose address is taken, that a reference binds to, or that a lambda captures by reference anywhere in
// the function: code in the selection could change them through the alias, so they are passed by reference.
class AliasFinder : public RecursiveASTVisitor<AliasFinder> {
  public:
    explicit AliasFinder(ASTContext &context) : context_(context) {}

    bool VisitDeclRefExpr(DeclRefExpr *reference) {
        if (const auto *variable = dyn_cast<VarDecl>(reference->getDecl())) {
            if (classifyUse(context_, reference) == VariableUse::Other) {
                aliased.insert(variable);
            }
        }
        return true;
    }

    bool VisitLambdaExpr(LambdaExpr *lambda) {
        for (const auto &capture : lambda->captures()) {
            if (capture.capturesVariable() && capture.getCaptureKind() == LCK_ByRef) {
                if (const auto *variable = dyn_cast<VarDecl>(capture.getCapturedVar())) {
                    aliased.insert(variable);
                }
            }
        }
        return true;
    }

    std::set<const VarDecl *> aliased;

  private:
    ASTContext &context_;
};

bool containsSourceLocationBuiltin(const Stmt *statement) {
    if (!statement) {
        return false;
    }
    if (isa<SourceLocExpr, PredefinedExpr>(statement)) {
        return true;
    }
    if (const auto *defaultArgument = dyn_cast<CXXDefaultArgExpr>(statement)) {
        return containsSourceLocationBuiltin(defaultArgument->getExpr());
    }
    for (const auto *child : statement->children()) {
        if (containsSourceLocationBuiltin(child)) {
            return true;
        }
    }
    return false;
}

class SelectionAnalyzer : public RecursiveASTVisitor<SelectionAnalyzer> {
    using Base = RecursiveASTVisitor<SelectionAnalyzer>;

  public:
    SelectionAnalyzer(IifePlanner &planner, ASTContext &context, bool isExpression)
        : planner_(planner), context_(context), isExpression_(isExpression) {}

    bool shouldVisitImplicitCode() const { return true; }

    bool TraverseLambdaExpr(LambdaExpr *lambda) {
        for (const auto &capture : lambda->captures()) {
            if (capture.capturesVariable()) {
                const auto *variable = dyn_cast<VarDecl>(capture.getCapturedVar());
                if (variable && variable->hasLocalStorage() && !planner_.isInSelection(variable->getLocation())) {
                    planner_.breakPrecondition(codes::CapturedVariable,
                                               "A lambda in the selection captures `" + variable->getNameAsString() +
                                                   "`, which would then capture the parameter of the new lambda.",
                                               capture.getLocation());
                }
            }
            if (capture.capturesThis()) {
                needsThis = true;
            }
        }
        ++nestedFunctionDepth_;
        bool result = TraverseStmt(lambda->getBody());
        --nestedFunctionDepth_;
        return result;
    }

    bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *expression) {
        return unevaluated([&] { return Base::TraverseUnaryExprOrTypeTraitExpr(expression); });
    }
    bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *expression) {
        return unevaluated([&] { return Base::TraverseCXXNoexceptExpr(expression); });
    }
    bool TraverseCXXTypeidExpr(CXXTypeidExpr *expression) {
        return unevaluated([&] { return Base::TraverseCXXTypeidExpr(expression); });
    }
    bool TraverseDecltypeTypeLoc(DecltypeTypeLoc type) {
        return unevaluated([&] { return Base::TraverseDecltypeTypeLoc(type); });
    }
    bool TraverseRequiresExpr(RequiresExpr *expression) {
        return unevaluated([&] { return Base::TraverseRequiresExpr(expression); });
    }

    bool TraverseForStmt(ForStmt *statement) {
        return loop([&] { return Base::TraverseForStmt(statement); });
    }
    bool TraverseWhileStmt(WhileStmt *statement) {
        return loop([&] { return Base::TraverseWhileStmt(statement); });
    }
    bool TraverseDoStmt(DoStmt *statement) {
        return loop([&] { return Base::TraverseDoStmt(statement); });
    }
    bool TraverseCXXForRangeStmt(CXXForRangeStmt *statement) {
        return loop([&] { return Base::TraverseCXXForRangeStmt(statement); });
    }
    bool TraverseSwitchStmt(SwitchStmt *statement) {
        ++switchDepth_;
        bool result = Base::TraverseSwitchStmt(statement);
        --switchDepth_;
        return result;
    }

    bool VisitCXXThisExpr(CXXThisExpr *) {
        needsThis = true;
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr *reference) {
        if (const auto *binding = dyn_cast<BindingDecl>(reference->getDecl())) {
            if (!planner_.isInSelection(binding->getLocation())) {
                planner_.breakPrecondition(codes::TypeNotNameable,
                                           "The selection uses the structured binding `" + binding->getNameAsString() +
                                               "`, which can't be passed as a parameter.",
                                           reference->getLocation());
            }
            return true;
        }
        const auto *variable = dyn_cast<VarDecl>(reference->getDecl());
        if (!variable || !variable->hasLocalStorage() || planner_.isInSelection(variable->getLocation())) {
            return true;
        }
        if (reference->refersToEnclosingVariableOrCapture() || nestedFunctionDepth_ > 0) {
            planner_.breakPrecondition(codes::CapturedVariable,
                                       "`" + variable->getNameAsString() +
                                           "` is captured by an enclosing lambda, which the new lambda can't refer to.",
                                       reference->getLocation());
            return true;
        }
        if (unevaluatedDepth_ > 0) {
            planner_.breakPrecondition(codes::UnevaluatedOperand,
                                       "`" + variable->getNameAsString() +
                                           "` is used in an unevaluated operand, such as `decltype`, where the "
                                           "parameter's type could differ from the variable's.",
                                       reference->getLocation());
        }
        if (std::find(variables.begin(), variables.end(), variable) == variables.end()) {
            variables.push_back(variable);
        }
        if (classifyUse(context_, reference) != VariableUse::Read) {
            notOnlyRead.insert(variable);
        }
        return true;
    }

    bool VisitStmt(Stmt *statement) {
        if (nestedFunctionDepth_ > 0) {
            return true;
        }
        auto location = statement->getBeginLoc();
        if (isa<ReturnStmt, CoreturnStmt>(statement)) {
            breakControlFlow("`return`", location);
        } else if (isa<GotoStmt, IndirectGotoStmt>(statement)) {
            breakControlFlow("`goto`", location);
        } else if (isa<LabelStmt, AddrLabelExpr>(statement)) {
            breakControlFlow("a label", location);
        } else if (isa<BreakStmt>(statement) && loopDepth_ == 0 && switchDepth_ == 0) {
            breakControlFlow("`break`", location);
        } else if (isa<ContinueStmt>(statement) && loopDepth_ == 0) {
            breakControlFlow("`continue`", location);
        } else if (isa<SwitchCase>(statement) && switchDepth_ == 0) {
            breakControlFlow("a `case` label", location);
        } else if (isa<CoroutineSuspendExpr>(statement)) {
            planner_.breakPrecondition(codes::ContainsCoroutineKeyword,
                                       "The selection contains `co_await` or `co_yield`, which would suspend the new "
                                       "lambda instead of the coroutine.",
                                       location);
        } else if (containsSourceLocationBuiltin(statement) &&
                   (isa<SourceLocExpr, PredefinedExpr, CXXDefaultArgExpr>(statement))) {
            planner_.breakPrecondition(codes::FunctionNameObserved,
                                       "The selection observes the name or location of its function, such as with "
                                       "`__func__` or `std::source_location::current()`, which would change.",
                                       location);
        } else if (isExpression_ && isa<MaterializeTemporaryExpr, CXXBindTemporaryExpr>(statement)) {
            planner_.breakPrecondition(codes::TemporaryLifetime,
                                       "The selection creates a temporary object, which would be destroyed when the "
                                       "lambda returns instead of at the end of the full-expression.",
                                       location);
        }
        return true;
    }

    std::vector<const VarDecl *> variables;
    std::set<const VarDecl *> notOnlyRead;
    bool needsThis = false;

  private:
    template <class Traverse> bool unevaluated(Traverse traverse) {
        ++unevaluatedDepth_;
        bool result = traverse();
        --unevaluatedDepth_;
        return result;
    }

    template <class Traverse> bool loop(Traverse traverse) {
        ++loopDepth_;
        bool result = traverse();
        --loopDepth_;
        return result;
    }

    void breakControlFlow(const std::string &what, SourceLocation location) {
        planner_.breakPrecondition(codes::NonLocalControlFlow,
                                   "The selection contains " + what +
                                       ", which would leave the new lambda instead of the enclosing code.",
                                   location);
    }

    IifePlanner &planner_;
    ASTContext &context_;
    bool isExpression_;
    int nestedFunctionDepth_ = 0;
    int unevaluatedDepth_ = 0;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
};

bool isScalarPassedByValueSafely(const VarDecl *variable) {
    auto type = variable->getType();
    return !type->isReferenceType() && type->isScalarType() && !type.isVolatileQualified();
}

bool containsUnnameableType(QualType type) {
    type = type.getNonReferenceType();
    while (true) {
        if (const auto *array = type->getAsArrayTypeUnsafe()) {
            type = array->getElementType();
        } else if (type->isPointerType()) {
            type = type->getPointeeType();
        } else {
            break;
        }
    }
    if (const auto *tag = type->getAsTagDecl()) {
        if (const auto *record = dyn_cast<CXXRecordDecl>(tag); record && record->isLambda()) {
            return true;
        }
        if (!tag->getIdentifier() && !tag->getTypedefNameForAnonDecl()) {
            return true;
        }
        if (tag->getDeclContext()->isFunctionOrMethod()) {
            return true;
        }
    }
    return false;
}

} // namespace

IifePlanner::IifePlanner(const Workspace &workspace, ExtractIifeRequest request)
    : workspace_(workspace), request_(std::move(request)) {}

void IifePlanner::breakPrecondition(const char *code, std::string message, SourceLocation location,
                                    std::string suggestion) {
    if (brokenPrecondition_) {
        return;
    }
    std::optional<FileRange> range;
    if (location.isValid() && context_) {
        if (auto offset =
                fileOffsetOf(context_->getSourceManager(), context_->getSourceManager().getExpansionLoc(location))) {
            range = rangeOf(workspace_, *offset);
        }
    }
    if (!range) {
        range = rangeOf(workspace_, FileOffset{file_, start_}, end_ - start_);
    }
    brokenPrecondition_ = makeRefusal(code, std::move(message), range, std::move(suggestion));
}

bool IifePlanner::isInSelection(SourceLocation location) const {
    auto offset = fileOffsetOf(context_->getSourceManager(), context_->getSourceManager().getExpansionLoc(location));
    return offset && offset->file == file_ && start_ <= offset->offset && offset->offset < end_;
}

std::optional<RefusalReason> IifePlanner::parseAndCollect() {
    parsed_ = parse(workspace_, {});
    if (!parsed_.parsedEveryTranslationUnit || !parsed_.errors.empty()) {
        std::string firstError = parsed_.errors.empty() ? "a translation unit could not be parsed" : parsed_.errors[0];
        return makeRefusal(codes::WorkspaceDoesNotCompile,
                           "The workspace must compile before it can be refactored; " + firstError + ".");
    }
    translationUnits_ = collectBindings(workspace_, parsed_);
    return std::nullopt;
}

std::optional<RefusalReason> IifePlanner::findSelection() {
    file_ = workspace_.absolutePath(request_.file);
    auto text = workspace_.read(file_);
    if (!text) {
        llvm::consumeError(text.takeError());
        return makeRefusal(codes::InvalidLocation, "Cannot read " + request_.file + ".");
    }
    text_ = *text;
    auto from = offsetOfPosition(text_, request_.from);
    auto to = offsetOfPosition(text_, request_.to);
    if (!from || !to || *from >= *to) {
        return makeRefusal(codes::InvalidLocation, "The selection is not a range in the file.");
    }
    start_ = *from;
    end_ = *to;
    while (start_ < end_ && isspace(static_cast<unsigned char>(text_[start_]))) {
        ++start_;
    }
    while (end_ > start_ && isspace(static_cast<unsigned char>(text_[end_ - 1]))) {
        --end_;
    }
    for (auto &unit : translationUnits_) {
        if (!unit.workspaceFiles.count(file_) && unit.mainFile != file_) {
            continue;
        }
        SelectionFinder finder(unit.unit->getASTContext(), file_, start_, end_);
        finder.TraverseDecl(unit.unit->getASTContext().getTranslationUnitDecl());
        if (finder.expression_ || finder.block_) {
            unit_ = &unit;
            context_ = &unit.unit->getASTContext();
            expression_ = finder.block_ ? nullptr : finder.expression_;
            block_ = finder.block_;
            statements_ = finder.statements_;
            selectionEndsBlock_ = finder.endsBlock_;
            return std::nullopt;
        }
    }
    return makeRefusal(codes::InvalidSelection,
                       "The selection is not exactly one expression, or whole statements of one block.",
                       rangeOf(workspace_, FileOffset{file_, start_}, end_ - start_),
                       "Select a complete expression, or complete statements.");
}

void IifePlanner::checkContext() {
    DynTypedNode node = expression_ ? DynTypedNode::create(*expression_) : DynTypedNode::create(*block_);
    auto refuseContext = [&](std::string message) { breakPrecondition(codes::UnsupportedContext, std::move(message)); };
    while (true) {
        auto parents = context_->getParents(node);
        if (parents.size() != 1) {
            refuseContext("The selection is not inside the body of a function.");
            return;
        }
        auto parent = parents[0];
        if (const auto *function = parent.get<FunctionDecl>()) {
            if (node.get<Stmt>() != function->getBody()) {
                refuseContext("The selection is not inside the body of the function.");
                return;
            }
            if (function->isDependentContext() || function->isTemplateInstantiation()) {
                refuseContext("The selection is inside a template, which the tool does not support yet.");
                return;
            }
            enclosingBody_ = function->getBody();
            return;
        }
        if (const auto *lambda = parent.get<LambdaExpr>()) {
            if (node.get<Stmt>() != lambda->getBody()) {
                refuseContext("The selection is in a lambda's captures.");
                return;
            }
            if (lambda->getCallOperator()->isDependentContext()) {
                refuseContext("The selection is inside a generic lambda or a template.");
                return;
            }
            enclosingBody_ = lambda->getBody();
            return;
        }
        if (const auto *statement = parent.get<Stmt>()) {
            if (isa<UnaryExprOrTypeTraitExpr, CXXNoexceptExpr, RequiresExpr>(statement) ||
                (isa<CXXTypeidExpr>(statement) && !cast<CXXTypeidExpr>(statement)->isPotentiallyEvaluated())) {
                breakPrecondition(codes::UnevaluatedOperand, "The selection is in an unevaluated operand.");
                return;
            }
            if (isa<ConstantExpr>(statement)) {
                refuseContext("The selection is in a constant expression, such as a `case` label.");
                return;
            }
            if (const auto *ifStatement = dyn_cast<IfStmt>(statement);
                ifStatement && ifStatement->isConstexpr() && node.get<Stmt>() == ifStatement->getCond()) {
                refuseContext("The selection is the condition of `if constexpr`.");
                return;
            }
            if (isa<CoroutineBodyStmt>(statement)) {
                breakPrecondition(codes::ContainsCoroutineKeyword, "The selection is in a coroutine.");
                return;
            }
            node = parent;
            continue;
        }
        if (const auto *variable = parent.get<VarDecl>()) {
            if (!variable->hasLocalStorage() || variable->isConstexpr()) {
                refuseContext("The selection initializes a static or constexpr variable, whose initialization "
                              "could stop being constant.");
                return;
            }
            node = parent;
            continue;
        }
        refuseContext("The selection is not inside the body of a function.");
        return;
    }
}

void IifePlanner::checkPreprocessor() {
    auto selected = llvm::StringRef(text_).substr(start_, end_ - start_);
    if (hasPreprocessorDirective(selected)) {
        breakPrecondition(codes::MacroInvolved, "The selection contains a preprocessor directive.");
    }
    auto overlaps = [&](const std::map<std::string, std::vector<std::pair<unsigned, unsigned>>> &ranges) {
        auto found = ranges.find(file_);
        if (found == ranges.end()) {
            return false;
        }
        return llvm::any_of(found->second,
                            [&](const auto &range) { return range.first < end_ && start_ < range.second; });
    };
    for (const auto &unit : translationUnits_) {
        if (overlaps(unit.macroExpansionRanges)) {
            breakPrecondition(codes::MacroInvolved,
                              "The selection uses a macro, whose expansion could depend on where it is.");
        }
        if (overlaps(unit.skippedRanges)) {
            breakPrecondition(codes::PreprocessorRegion, "The selection contains a region that `#if` skips.");
        }
    }
}

void IifePlanner::checkExpressionSelection() {
    const auto *expression = expression_;
    if (isa<InitListExpr>(ignoringParens(expression))) {
        breakPrecondition(codes::InvalidSelection, "A braced initializer list is not an expression.");
    }
    if (expression->hasPlaceholderType() || expression->getType()->isFunctionType()) {
        breakPrecondition(codes::TargetTypedExpression,
                          "The selection names a function or an overload set, whose meaning depends on its use.");
    }
    if (expression->refersToBitField()) {
        breakPrecondition(codes::BitField, "The selection is a bit-field, which a lambda can't return by reference.");
    }
    const auto *parent = parentIgnoringParens(*context_, expression);
    if (const auto *cast = dyn_cast_or_null<ImplicitCastExpr>(parent)) {
        auto kind = cast->getCastKind();
        if (kind == CK_NullToPointer || kind == CK_NullToMemberPointer) {
            breakPrecondition(codes::TargetTypedExpression,
                              "The selection is a null pointer constant, which a lambda would return as a number.");
        }
    }
    if (isa<StringLiteral>(ignoringParens(expression)) &&
        !(isa_and_nonnull<ImplicitCastExpr>(parent) &&
          cast<ImplicitCastExpr>(parent)->getCastKind() == CK_ArrayToPointerDecay)) {
        breakPrecondition(codes::TargetTypedExpression, "The selected string literal initializes an array.");
    }
    if (expression->isPRValue() && expression->getType()->isRecordType() && expression->getType().hasQualifiers()) {
        breakPrecondition(codes::TypeNotNameable,
                          "The selection is a cv-qualified class prvalue, whose qualifiers a lambda would drop.");
    }
}

void IifePlanner::checkStatementSelection() {
    for (const auto *statement : statements_) {
        const auto *declarations = dyn_cast<DeclStmt>(statement);
        if (!declarations) {
            continue;
        }
        for (const auto *declaration : declarations->decls()) {
            const auto *named = dyn_cast<NamedDecl>(declaration);
            if (!named) {
                continue;
            }
            auto identity = identityOf(named);
            for (const auto &[key, identities] : unit_->bindings) {
                if (identities.count(identity) && key.location.file == file_ &&
                    (key.location.offset < start_ || key.location.offset >= end_)) {
                    breakPrecondition(codes::DeclarationEscapes,
                                      "`" + named->getNameAsString() +
                                          "` is declared in the selection and used after it, so it can't move into "
                                          "the lambda.",
                                      named->getLocation());
                }
            }
            const auto *variable = dyn_cast<VarDecl>(named);
            if (!variable || selectionEndsBlock_) {
                continue;
            }
            bool extendsTemporary = false;
            if (variable->getType()->isReferenceType() && variable->getInit()) {
                const auto *initializer = variable->getInit()->IgnoreImplicit();
                extendsTemporary = isa<MaterializeTemporaryExpr>(variable->getInit()) ||
                                   isa<MaterializeTemporaryExpr>(initializer) || variable->getInit()->isPRValue();
                if (const auto *cleanups = dyn_cast<ExprWithCleanups>(variable->getInit())) {
                    extendsTemporary = extendsTemporary || isa<MaterializeTemporaryExpr>(cleanups->getSubExpr());
                }
            }
            if (variable->needsDestruction(*context_) || extendsTemporary) {
                breakPrecondition(codes::DestructorTiming,
                                  "`" + variable->getNameAsString() +
                                      "` would be destroyed when the lambda returns, before the rest of its block "
                                      "runs.",
                                  variable->getLocation(), "Extend the selection to the end of the block.");
            }
        }
    }
    if (hasLiteralSpanningLines(llvm::StringRef(text_).substr(start_, end_ - start_))) {
        breakPrecondition(codes::UnsupportedContext,
                          "The selection has a literal that spans lines, which re-indenting would change.");
    }
}

std::optional<std::string> IifePlanner::typeForParameter(const VarDecl *variable, bool byReference) {
    auto type = variable->getType().getNonReferenceType();
    if (containsUnnameableType(type)) {
        return std::nullopt;
    }
    auto policy = context_->getPrintingPolicy();
    policy.SuppressScope = false;
    policy.FullyQualifiedName = true;
    policy.SuppressUnwrittenScope = true;
    auto qualified = TypeName::getFullyQualifiedType(type, *context_, /*WithGlobalNsPrefix=*/false);
    auto printed = qualified.getAsString(policy);
    if (llvm::StringRef(printed).contains("auto") || llvm::StringRef(printed).contains("decltype") ||
        llvm::StringRef(printed).contains("(lambda")) {
        qualified = TypeName::getFullyQualifiedType(type.getCanonicalType(), *context_, false);
    }
    if (byReference) {
        qualified = context_->getLValueReferenceType(qualified);
    }
    std::string declaration;
    llvm::raw_string_ostream out(declaration);
    qualified.print(out, policy, variable->getName());
    return out.str();
}

void IifePlanner::analyzeVariables() {
    SelectionAnalyzer analyzer(*this, *context_, expression_ != nullptr);
    if (expression_) {
        analyzer.TraverseStmt(const_cast<Expr *>(expression_));
    } else {
        for (const auto *statement : statements_) {
            analyzer.TraverseStmt(const_cast<Stmt *>(statement));
        }
    }
    needsThis_ = analyzer.needsThis;
    AliasFinder aliases(*context_);
    if (enclosingBody_) {
        aliases.TraverseStmt(const_cast<Stmt *>(enclosingBody_));
    }
    passedVariables_ = analyzer.variables;
    for (const auto *variable : passedVariables_) {
        IifeParameter parameter;
        parameter.variable = variable;
        parameter.byReference = !isScalarPassedByValueSafely(variable) || aliases.aliased.count(variable) ||
                                analyzer.notOnlyRead.count(variable);
        auto declaration = typeForParameter(variable, parameter.byReference);
        if (!declaration) {
            breakPrecondition(codes::TypeNotNameable,
                              "The type of `" + variable->getNameAsString() + "` can't be written out.",
                              variable->getLocation());
            declaration = "auto &" + variable->getNameAsString();
        }
        parameter.declaration = *declaration;
        parameters_.push_back(parameter);
    }
}

std::optional<RefusalReason> IifePlanner::analyzeSelection() {
    if (auto refusal = findSelection()) {
        return refusal;
    }
    checkPreprocessor();
    checkContext();
    if (expression_) {
        checkExpressionSelection();
    } else {
        checkStatementSelection();
    }
    analyzeVariables();
    return std::nullopt;
}

std::optional<RefusalReason> IifePlanner::writeEdits() {
    std::string header = "[";
    if (needsThis_) {
        header += "this";
    }
    header += "](";
    for (size_t index = 0; index < parameters_.size(); ++index) {
        if (index > 0) {
            header += ", ";
        }
        auto &parameter = parameters_[index];
        auto nameAt = parameter.declaration.rfind(parameter.variable->getName().str());
        parameter.nameOffsetInHeader = header.size() + nameAt;
        header += parameter.declaration;
    }
    header += ")";
    std::string arguments;
    for (const auto *variable : passedVariables_) {
        arguments += (arguments.empty() ? "" : ", ") + variable->getNameAsString();
    }
    std::string footer;
    if (expression_) {
        if (expression_->isPRValue()) {
            header += " { return ";
            footer = "; }(" + arguments + ")";
        } else {
            header += " -> decltype(auto) { return (";
            footer = "); }(" + arguments + ")";
        }
    } else {
        auto lineStart = llvm::StringRef(text_).substr(0, start_).rfind('\n') + 1;
        auto indentation = text_.substr(lineStart, start_ - lineStart);
        if (llvm::StringRef(indentation).find_first_not_of(" \t") != llvm::StringRef::npos) {
            return makeRefusal(codes::InvalidSelection, "The selected statements must start their line.",
                               rangeOf(workspace_, FileOffset{file_, start_}));
        }
        std::string indentUnit = llvm::StringRef(indentation).contains('\t') ? "\t" : "    ";
        header += " {\n" + indentation + indentUnit;
        for (unsigned offset = start_; offset < end_; ++offset) {
            if (text_[offset] == '\n' && offset + 1 < end_ && text_[offset + 1] != '\n') {
                edits_.add(file_, Replacement{offset + 1, 0, indentUnit});
            }
        }
        footer = "\n" + indentation + "}(" + arguments + ");";
    }
    headerLength_ = header.size();
    footerLength_ = footer.size();
    edits_.add(file_, Replacement{start_, 0, header});
    edits_.add(file_, Replacement{end_, 0, footer});
    return std::nullopt;
}

std::optional<RefusalReason> IifePlanner::verify() {
    auto overlay = applyEdits(workspace_, edits_);
    auto parsedAfter = parse(workspace_, overlay);
    auto failed = [&](std::string message) {
        return makeRefusal(codes::InternalVerificationFailed,
                           "The tool's check of its own result failed, which is a bug in the tool; please report it. " +
                               message,
                           rangeOf(workspace_, FileOffset{file_, start_}, end_ - start_));
    };
    if (!parsedAfter.parsedEveryTranslationUnit || !parsedAfter.errors.empty()) {
        return failed("The result does not compile: " + (parsedAfter.errors.empty()
                                                             ? std::string("a translation unit could not be parsed")
                                                             : parsedAfter.errors[0]));
    }
    auto after = collectBindings(workspace_, parsedAfter);
    unsigned headerBegin = start_;
    unsigned headerEnd = edits_.mapOffset(file_, start_);
    unsigned footerEnd = edits_.mapOffset(file_, end_);
    unsigned footerBegin = footerEnd - footerLength_;
    std::map<DeclIdentity, DeclIdentity> parameterOf;
    for (const auto &parameter : parameters_) {
        parameterOf[identityOf(parameter.variable)] =
            DeclIdentity{FileOffset{file_, headerBegin + parameter.nameOffsetInHeader}, "ParmVar"};
    }
    BindingExpectations expectations;
    expectations.substitute = [&](const BindingKey &key, const DeclIdentity &identity) -> std::optional<DeclIdentity> {
        if (key.location.file != file_ || key.location.offset < start_ || key.location.offset >= end_) {
            return std::nullopt;
        }
        if (auto found = parameterOf.find(identity); found != parameterOf.end()) {
            return found->second;
        }
        return std::nullopt;
    };
    expectations.isNew = [&](const BindingKey &key) {
        if (key.location.file != file_) {
            return false;
        }
        auto offset = key.location.offset;
        return (headerBegin <= offset && offset < headerEnd) || (footerBegin <= offset && offset < footerEnd);
    };
    if (auto difference = compareBindings(translationUnits_, after, edits_, expectations)) {
        return failed("A reference would bind differently: " + difference->description + ".");
    }
    if (expression_) {
        for (auto &unit : after) {
            if (unit.mainFile != unit_->mainFile) {
                continue;
            }
            struct CallFinder : RecursiveASTVisitor<CallFinder> {
                const SourceManager *sourceManager;
                std::string file;
                unsigned offset;
                const Expr *call = nullptr;
                bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *expression) {
                    auto begin = fileOffsetOf(*sourceManager, expression->getBeginLoc());
                    if (!call && begin && begin->file == file && begin->offset == offset &&
                        isa<LambdaExpr>(expression->getArg(0)->IgnoreImplicit())) {
                        call = expression;
                    }
                    return true;
                }
            } finder;
            finder.sourceManager = &unit.unit->getSourceManager();
            finder.file = file_;
            finder.offset = headerBegin;
            finder.TraverseDecl(unit.unit->getASTContext().getTranslationUnitDecl());
            if (!finder.call) {
                return failed("The new lambda call was not found.");
            }
            auto beforeType = expression_->getType().getCanonicalType().getAsString();
            auto afterType = finder.call->getType().getCanonicalType().getAsString();
            if (beforeType != afterType || expression_->getValueKind() != finder.call->getValueKind()) {
                return failed("The lambda call's type or value category (" + afterType +
                              ") differs from the expression's (" + beforeType + ").");
            }
        }
    }
    return std::nullopt;
}

RefactoringResult extractIife(const Workspace &workspace, const ExtractIifeRequest &request) {
    auto refuse = [&](RefusalReason reason) { return RefactoringResult::refused(workspace.root(), std::move(reason)); };
    IifePlanner planner(workspace, request);
    if (auto refusal = planner.parseAndCollect()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.analyzeSelection()) {
        return refuse(*refusal);
    }
    if (planner.firstBrokenPrecondition()) {
        return refuse(*planner.firstBrokenPrecondition());
    }
    if (auto refusal = planner.writeEdits()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.verify()) {
        return refuse(*refusal);
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.edits()));
}

} // namespace riktig
