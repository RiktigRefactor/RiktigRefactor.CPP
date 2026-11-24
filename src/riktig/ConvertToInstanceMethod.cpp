#include "ConvertToInstanceMethod.h"

#include "Independence.h"
#include "NameChecks.h"
#include "RawTokens.h"
#include "RefusalCodes.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

namespace riktig {

using namespace clang;
namespace codes = refusal_codes;

namespace {

const Stmt *parentIgnoringParensAndImplicitCasts(ASTContext &context, const Stmt *statement) {
    auto parents = context.getParents(*statement);
    while (parents.size() == 1) {
        const auto *parent = parents[0].get<Stmt>();
        if (!parent || !isa<ParenExpr, ImplicitCastExpr>(parent)) {
            return parent;
        }
        parents = context.getParents(*parent);
    }
    return nullptr;
}

const ParmVarDecl *receiverParameterOf(const FunctionDecl *declaration, unsigned index) {
    return index < declaration->getNumParams() ? declaration->getParamDecl(index) : nullptr;
}

class ParameterUseFinder : public RecursiveASTVisitor<ParameterUseFinder> {
  public:
    explicit ParameterUseFinder(const ParmVarDecl *parameter) : parameter_(parameter) {}

    bool VisitDeclRefExpr(DeclRefExpr *reference) {
        if (reference->getDecl() == parameter_) {
            uses.push_back(reference);
        }
        return true;
    }
    bool VisitLambdaExpr(LambdaExpr *lambda) {
        for (const auto &capture : lambda->captures()) {
            if (capture.capturesVariable() && capture.getCapturedVar() == parameter_ &&
                (capture.isExplicit() || capture.getCaptureKind() != LCK_ByRef) && !capturingLambda) {
                capturingLambda = lambda;
            }
        }
        return true;
    }

    std::vector<const DeclRefExpr *> uses;
    const LambdaExpr *capturingLambda = nullptr;

  private:
    const ParmVarDecl *parameter_;
};

// Whether the argument reaches the receiver parameter with no conversion but a derived-to-base one.
bool bindsWithoutConversion(const Expr *argument, const CXXRecordDecl *receiverClass) {
    const Expr *expression = argument;
    while (true) {
        expression = expression->IgnoreParens();
        if (const auto *cast = dyn_cast<ImplicitCastExpr>(expression)) {
            auto kind = cast->getCastKind();
            if (kind != CK_NoOp && kind != CK_DerivedToBase && kind != CK_UncheckedDerivedToBase) {
                return false;
            }
            expression = cast->getSubExpr();
            continue;
        }
        if (const auto *materialized = dyn_cast<MaterializeTemporaryExpr>(expression)) {
            expression = materialized->getSubExpr();
            continue;
        }
        if (const auto *bound = dyn_cast<CXXBindTemporaryExpr>(expression)) {
            expression = bound->getSubExpr();
            continue;
        }
        break;
    }
    if (const auto *construction = dyn_cast<CXXConstructExpr>(expression);
        construction && !isa<CXXTemporaryObjectExpr>(construction) && !construction->isListInitialization()) {
        return false;
    }
    const auto *record = expression->getType()->getAsCXXRecordDecl();
    if (!record || !record->hasDefinition()) {
        return false;
    }
    return record->getCanonicalDecl() == receiverClass->getCanonicalDecl() ||
           record->isDerivedFrom(receiverClass->getDefinition());
}

} // namespace

ConvertToInstanceMethodPlanner::ConvertToInstanceMethodPlanner(const Workspace &workspace,
                                                         ConvertToInstanceMethodRequest request)
    : workspace_(workspace), request_(std::move(request)), plan_(workspace) {}

std::optional<FileRange> ConvertToInstanceMethodPlanner::rangeAt(const ASTContext &context,
                                                              SourceLocation location) const {
    auto offset = fileOffsetOf(context.getSourceManager(), context.getSourceManager().getExpansionLoc(location));
    if (!offset) {
        return std::nullopt;
    }
    return rangeOf(workspace_, *offset);
}

void ConvertToInstanceMethodPlanner::breakPrecondition(const char *code, std::string message, const ASTContext *context,
                                                    SourceLocation location, std::string suggestion) {
    if (brokenPrecondition_) {
        return;
    }
    std::optional<FileRange> range;
    if (context && location.isValid()) {
        range = rangeAt(*context, location);
    }
    brokenPrecondition_ = makeRefusal(code, std::move(message), range, std::move(suggestion));
}

std::optional<RefusalReason> ConvertToInstanceMethodPlanner::parseAndCollect() {
    return loadWorkspace(workspace_, loaded_);
}

std::optional<RefusalReason> ConvertToInstanceMethodPlanner::findMethodAndReceiver() {
    const NamedDecl *declaration = nullptr;
    if (auto refusal = findDeclarationAtCursor(workspace_, loaded_, request_.file, request_.at, declaration)) {
        return refusal;
    }
    const auto &context = declaration->getASTContext();
    if (isa<FunctionTemplateDecl>(declaration)) {
        return makeRefusal(codes::TemplateInvolved, "The tool does not change function templates.",
                           rangeAt(context, declaration->getLocation()));
    }
    const auto *function = dyn_cast<FunctionDecl>(declaration);
    if (!function || !function->getDeclName().isIdentifier()) {
        return makeRefusal(codes::NotAStaticMember,
                           "`" + declaration->getNameAsString() + "` is not a function named by an identifier.",
                           rangeAt(context, declaration->getLocation()));
    }
    const auto *method = dyn_cast<CXXMethodDecl>(function);
    if (method && !method->isStatic()) {
        return makeRefusal(codes::NotAStaticMember,
                           "`" + function->getNameAsString() + "` is already an instance method.",
                           rangeAt(context, declaration->getLocation()));
    }
    std::optional<unsigned> index;
    for (unsigned position = 0; position < function->getNumParams(); ++position) {
        if (function->getParamDecl(position)->getName() == request_.receiver) {
            index = position;
        }
    }
    if (!index) {
        return makeRefusal(codes::InvalidParameter,
                           "`" + function->getNameAsString() + "` has no parameter named `" + request_.receiver + "`.",
                           rangeAt(context, declaration->getLocation()));
    }
    receiverIndex_ = *index;
    auto type = function->getParamDecl(*index)->getType();
    if (!type->isLValueReferenceType() || !type.getNonReferenceType()->getAsCXXRecordDecl() ||
        type.getNonReferenceType().isVolatileQualified()) {
        return makeRefusal(codes::UnsupportedParameter,
                           "The receiver parameter must be a reference to a class, such as `const Order &`; the tool "
                           "does not take a pointer or a copy as the receiver.",
                           rangeAt(context, function->getParamDecl(*index)->getLocation()));
    }
    receiverIsConst_ = type.getNonReferenceType().isConstQualified();
    receiverClass_ = type.getNonReferenceType()->getAsCXXRecordDecl();
    if (!method || method->getParent()->getCanonicalDecl() != receiverClass_->getCanonicalDecl()) {
        return makeRefusal(codes::UnsupportedTarget,
                           "`" + function->getNameAsString() + "` is not a static member of `" +
                               receiverClass_->getNameAsString() + "`, the receiver's class.",
                           rangeAt(context, declaration->getLocation()),
                           "Move it into `" + receiverClass_->getNameAsString() + "` with Move static member first.");
    }
    method_ = cast<CXXMethodDecl>(method->getCanonicalDecl());
    for (const auto *found : declarationsOf(loaded_, method_)) {
        declarations_.push_back(cast<CXXMethodDecl>(found));
    }
    references_ = findReferences(workspace_, loaded_, method_);
    return std::nullopt;
}

bool ConvertToInstanceMethodPlanner::isInBody(const ASTContext &context, SourceLocation location) const {
    auto offset = fileOffsetOf(context.getSourceManager(), context.getSourceManager().getExpansionLoc(location));
    if (!offset) {
        return false;
    }
    for (const auto *declaration : declarations_) {
        if (!declaration->doesThisDeclarationHaveABody()) {
            continue;
        }
        auto body = fileRangeOf(declaration->getASTContext(), declaration->getBody()->getSourceRange());
        if (body && body->file == offset->file && body->begin <= offset->offset && offset->offset < body->end) {
            return true;
        }
    }
    return false;
}

void ConvertToInstanceMethodPlanner::checkPreconditions() {
    const auto &context = method_->getASTContext();
    auto name = method_->getNameAsString();
    auto location = method_->getLocation();
    auto identity = identityOf(method_);
    if (identity.location.file.empty() || !workspace_.contains(identity.location.file)) {
        breakPrecondition(codes::DeclaredOutsideWorkspace, "`" + name + "` is declared outside the workspace.");
    }
    if (identity.tag.find('@') != std::string::npos) {
        breakPrecondition(codes::MacroInvolved, "`" + name + "` is declared by a macro.", &context, location);
    }
    if (receiverClass_->isDependentContext() || isa<ClassTemplateSpecializationDecl>(receiverClass_) ||
        method_->getTemplatedKind() != FunctionDecl::TK_NonTemplate) {
        breakPrecondition(codes::TemplateInvolved, "`" + name + "` is a member of a template.", &context, location);
    }
    if (method_->isDeleted() || method_->isVariadic() || receiverClass_->isLambda() || receiverClass_->isUnion()) {
        breakPrecondition(codes::UnsupportedMethod, "`" + name + "` is deleted or variadic, or its class is a union.",
                          &context, location);
    }
    for (const auto &unit : loaded_.units) {
        if (unit.macroNames.count(name)) {
            breakPrecondition(codes::MacroInvolved, "`" + name + "` is also the name of a macro.");
        }
    }
    for (const auto *declaration : declarations_) {
        const auto &declarationContext = declaration->getASTContext();
        const auto *parameter = receiverParameterOf(declaration, receiverIndex_);
        if (declaration->getFriendObjectKind() != Decl::FOK_None) {
            breakPrecondition(codes::UnsupportedMethod, "A friend declaration names `" + name + "`.",
                              &declarationContext, declaration->getLocation());
        }
        if (parameter && parameter->hasDefaultArg()) {
            breakPrecondition(codes::UnsupportedParameter, "The receiver parameter has a default argument.",
                              &declarationContext, parameter->getLocation());
        }
        if (declaration->doesThisDeclarationHaveABody()) {
            ParameterUseFinder finder(parameter);
            finder.TraverseStmt(declaration->getBody());
            if (finder.capturingLambda) {
                breakPrecondition(codes::ReceiverCaptured,
                                  "A lambda captures the receiver parameter explicitly or by copy; after the change "
                                  "it would capture `this`.",
                                  &declarationContext, finder.capturingLambda->getBeginLoc());
            }
        }
    }
    if (auto refusal = checkEveryMentionIsUnderstood(workspace_, loaded_.units, {name}, {})) {
        brokenPrecondition_ = brokenPrecondition_ ? brokenPrecondition_ : refusal;
    }
    for (const auto &reference : references_) {
        const auto &referenceContext = *reference.context;
        auto at = reference.expression->getExprLoc();
        if (!reference.inWorkspace) {
            breakPrecondition(codes::ReferenceOutsideWorkspace, "Code outside the workspace refers to `" + name + "`.");
            continue;
        }
        if (reference.inMacro) {
            breakPrecondition(codes::MacroInvolved, "A macro refers to `" + name + "`.", &referenceContext, at);
            continue;
        }
        if (reference.inTemplateInstantiation) {
            breakPrecondition(codes::TemplateInvolved, "A template refers to `" + name + "`.", &referenceContext, at);
            continue;
        }
        if (!reference.call) {
            breakPrecondition(codes::MemberFunctionPointer,
                              "`" + name +
                                  "` is used other than by calling it, such as taking its address, whose "
                                  "type would change.",
                              &referenceContext, at);
            continue;
        }
        if (const auto *member = dyn_cast<MemberExpr>(reference.expression); member && !member->isImplicitAccess()) {
            breakPrecondition(codes::UnsupportedReference,
                              "`" + name + "` is called through an object, which the tool does not rewrite yet.",
                              &referenceContext, at);
            continue;
        }
        if (receiverIndex_ >= reference.call->getNumArgs()) {
            breakPrecondition(codes::UnsupportedReference, "A call has too few arguments.", &referenceContext, at);
            continue;
        }
        const auto *receiver = reference.call->getArg(receiverIndex_);
        if (!bindsWithoutConversion(receiver, receiverClass_)) {
            breakPrecondition(codes::ReceiverConversion,
                              "The receiver argument is converted to `" + receiverClass_->getNameAsString() +
                                  "`, which would happen at another time or not at all.",
                              &referenceContext, receiver->getBeginLoc());
            continue;
        }
        IndependenceAnalysis independence(*reference.context, reference.enclosingFunction);
        for (const auto *argument : writtenArguments(reference.call)) {
            if (argument != receiver && !independence.areIndependent(receiver, argument)) {
                breakPrecondition(codes::EvaluationOrder,
                                  "The receiver would be evaluated before the other arguments, and the tool can't "
                                  "show it is independent of this one.",
                                  &referenceContext, argument->getBeginLoc(),
                                  "Store the receiver or the argument in a local variable first.");
                break;
            }
        }
    }
}

std::optional<RefusalReason> ConvertToInstanceMethodPlanner::rewriteCall(const Reference &reference) {
    const auto &context = *reference.context;
    auto failed = [&](std::string message) {
        return makeRefusal(codes::UnsupportedReference, std::move(message),
                           rangeAt(context, reference.expression->getExprLoc()));
    };
    SourceLocation nameLocation;
    SourceLocation calleeBegin;
    if (const auto *declRef = dyn_cast<DeclRefExpr>(reference.expression)) {
        nameLocation = declRef->getLocation();
        calleeBegin = declRef->hasQualifier() ? declRef->getQualifierLoc().getBeginLoc() : nameLocation;
    } else {
        const auto *member = cast<MemberExpr>(reference.expression);
        nameLocation = member->getMemberLoc();
        calleeBegin = member->getQualifierLoc() ? member->getQualifierLoc().getBeginLoc() : nameLocation;
    }
    auto name = fileOffsetOfLocation(context, nameLocation);
    auto begin = fileOffsetOfLocation(context, calleeBegin);
    if (!name || !begin || begin->file != name->file) {
        return failed("The call is written in a macro.");
    }
    const auto &file = name->file;
    auto arguments = writtenArguments(reference.call);
    const auto *receiver = reference.call->getArg(receiverIndex_);
    std::vector<OffsetRange> ranges;
    for (const auto *argument : arguments) {
        auto range = fileRangeOf(context, argument->getSourceRange());
        if (!range || range->file != file) {
            return failed("An argument is written in a macro.");
        }
        ranges.push_back(*range);
    }
    if (receiverIndex_ >= ranges.size()) {
        return failed("The receiver argument is not written out.");
    }
    const auto &receiverRange = ranges[receiverIndex_];
    unsigned deleteFrom = receiverRange.begin;
    unsigned deleteTo = receiverRange.end;
    if (receiverIndex_ + 1 < ranges.size()) {
        deleteTo = ranges[receiverIndex_ + 1].begin;
    } else if (receiverIndex_ > 0) {
        deleteFrom = ranges[receiverIndex_ - 1].end;
    }

    // The receiver goes before the name: `recv.name(`, `ptr->name(`, or `this->name(` for the receiver parameter
    // itself in a recursive call.
    const auto *written = receiver->IgnoreParenImpCasts();
    std::string calleePrefix;
    std::optional<OffsetRange> moved;
    unsigned movedPosition = 0;
    bool inBody = isInBody(context, nameLocation);
    if (const auto *parameter = dyn_cast<DeclRefExpr>(written);
        inBody && parameter && isa<ParmVarDecl>(parameter->getDecl()) &&
        parameter->getDecl()->getName() == request_.receiver &&
        parameter->getDecl()->getDeclContext() == reference.enclosingFunction) {
        calleePrefix = "this->";
        receiverParameterRangesInCalls_.push_back(receiverRange);
    } else if (const auto *unary = dyn_cast<UnaryOperator>(written);
               unary && unary->getOpcode() == UO_Deref && receiver->IgnoreImpCasts() == unary) {
        auto pointer = fileRangeOf(context, unary->getSubExpr()->getSourceRange());
        if (!pointer || pointer->file != file) {
            return failed("The receiver is written in a macro.");
        }
        bool simple = isa<DeclRefExpr, MemberExpr, CallExpr, CXXThisExpr, ParenExpr, ArraySubscriptExpr>(
            unary->getSubExpr()->IgnoreImpCasts());
        calleePrefix = simple ? textOf(plan_, *pointer) + "->" : "(" + textOf(plan_, *pointer) + ")->";
        moved = pointer;
        movedPosition = simple ? 0 : 1;
    } else {
        bool simple = isa<DeclRefExpr, MemberExpr, CallExpr, ParenExpr, ArraySubscriptExpr>(receiver->IgnoreImpCasts());
        calleePrefix = simple ? textOf(plan_, receiverRange) + "." : "(" + textOf(plan_, receiverRange) + ").";
        moved = receiverRange;
        movedPosition = simple ? 0 : 1;
    }
    // In a recursive call, the receiver may be built from the receiver parameter, whose uses change too.
    std::vector<Replacement> innerEdits;
    if (moved && inBody) {
        auto originalText = textOf(plan_, *moved);
        for (const auto *declaration : declarations_) {
            if (!declaration->doesThisDeclarationHaveABody()) {
                continue;
            }
            ParameterUseFinder finder(receiverParameterOf(declaration, receiverIndex_));
            finder.TraverseStmt(declaration->getBody());
            for (const auto *use : finder.uses) {
                auto useRange = fileRangeOf(declaration->getASTContext(), use->getSourceRange());
                if (!useRange || useRange->file != file || useRange->begin < moved->begin ||
                    useRange->end > moved->end) {
                    continue;
                }
                auto edit = editForParameterUse(declaration->getASTContext(), use);
                if (!edit) {
                    return failed("The receiver is written in a macro.");
                }
                edit->offset -= moved->begin;
                innerEdits.push_back(*edit);
                usesRewrittenInMovedReceivers_.insert(FileOffset{file, useRange->begin});
            }
        }
        EditMap innerMap;
        for (const auto &edit : innerEdits) {
            innerMap.add("", edit);
        }
        calleePrefix = calleePrefix.substr(0, movedPosition) + innerMap.apply("", originalText) +
                       calleePrefix.substr(movedPosition + originalText.size());
    }
    if (!plan_.replace(file, begin->offset, name->offset - begin->offset, calleePrefix) ||
        !plan_.replace(file, deleteFrom, deleteTo - deleteFrom, "")) {
        return failed("The call overlaps another call that changes, such as one in its receiver.");
    }
    if (moved) {
        plan_.relocate(file, moved->begin, moved->end - moved->begin, file, begin->offset, name->offset - begin->offset,
                       movedPosition, innerEdits);
    }
    return std::nullopt;
}

std::optional<RefusalReason> ConvertToInstanceMethodPlanner::rewriteDeclaration(const CXXMethodDecl *declaration) {
    const auto &context = declaration->getASTContext();
    auto failed = [&](std::string message) {
        return makeRefusal(codes::UnsupportedMethod, std::move(message), rangeAt(context, declaration->getLocation()));
    };
    std::vector<OffsetRange> parameters;
    for (const auto *parameter : declaration->parameters()) {
        auto range = fileRangeOf(context, parameter->getSourceRange());
        if (!range) {
            return failed("A parameter is written in a macro.");
        }
        parameters.push_back(*range);
    }
    if (receiverIndex_ >= parameters.size()) {
        return failed("The declaration has no receiver parameter.");
    }
    const auto &file = parameters[receiverIndex_].file;
    unsigned deleteFrom = parameters[receiverIndex_].begin;
    unsigned deleteTo = parameters[receiverIndex_].end;
    if (receiverIndex_ + 1 < parameters.size()) {
        deleteTo = parameters[receiverIndex_ + 1].begin;
    } else if (receiverIndex_ > 0) {
        deleteFrom = parameters[receiverIndex_ - 1].end;
    }
    if (!plan_.replace(file, deleteFrom, deleteTo - deleteFrom, "")) {
        return failed("The parameter list overlaps another edit.");
    }
    if (receiverIsConst_) {
        auto typeLoc = declaration->getFunctionTypeLoc();
        auto rparen = typeLoc ? fileOffsetOfLocation(context, typeLoc.getRParenLoc()) : std::nullopt;
        if (!rparen || rparen->file != file) {
            return failed("The parameter list is written in a macro.");
        }
        if (!plan_.insert(file, rparen->offset + 1, " const")) {
            return failed("The parameter list overlaps another edit.");
        }
    }
    if (declaration->getLexicalDeclContext() == declaration->getParent()) {
        auto begin = fileOffsetOfLocation(context, declaration->getBeginLoc());
        auto name = fileOffsetOfLocation(context, declaration->getLocation());
        if (!begin || !name || begin->file != name->file) {
            return failed("The declaration is written in a macro.");
        }
        const auto &text = plan_.text(begin->file);
        std::optional<unsigned> staticOffset;
        forEachRawIdentifier(llvm::StringRef(text).slice(begin->offset, name->offset),
                             [&](const RawIdentifier &identifier) {
                                 if (identifier.text == "static" && !staticOffset) {
                                     staticOffset = begin->offset + identifier.offset;
                                 }
                             });
        if (!staticOffset) {
            return failed("The `static` keyword is not written before the name.");
        }
        unsigned end = *staticOffset + 6;
        while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
            ++end;
        }
        if (!plan_.replace(begin->file, *staticOffset, end - *staticOffset, "")) {
            return failed("The declaration overlaps another edit.");
        }
    }
    return std::nullopt;
}

std::optional<Replacement> ConvertToInstanceMethodPlanner::editForParameterUse(ASTContext &context,
                                                                            const DeclRefExpr *use) const {
    auto range = fileRangeOf(context, use->getSourceRange());
    if (!range) {
        return std::nullopt;
    }
    const auto *parent = parentIgnoringParensAndImplicitCasts(context, use);
    if (const auto *member = dyn_cast_or_null<MemberExpr>(parent);
        member && !member->isArrow() && member->getBase()->IgnoreParenImpCasts() == use) {
        // `order.price` becomes the implicit member access `price`.
        auto location = member->getQualifierLoc() ? member->getQualifierLoc().getBeginLoc() : member->getMemberLoc();
        auto memberOffset = fileOffsetOfLocation(context, location);
        if (!memberOffset || memberOffset->file != range->file) {
            return std::nullopt;
        }
        return Replacement{range->begin, memberOffset->offset - range->begin, ""};
    }
    // An overloaded operator's operand may be postfix, where `*this` would need parentheses.
    bool bare = !parent || (isa<CallExpr, CXXConstructExpr, ReturnStmt, BinaryOperator, InitListExpr>(parent) &&
                            !isa<CXXOperatorCallExpr>(parent));
    return Replacement{range->begin, range->end - range->begin, bare ? "*this" : "(*this)"};
}

std::optional<RefusalReason> ConvertToInstanceMethodPlanner::rewriteBody(const CXXMethodDecl *definition) {
    auto &context = definition->getASTContext();
    ParameterUseFinder finder(receiverParameterOf(definition, receiverIndex_));
    finder.TraverseStmt(definition->getBody());
    for (const auto *use : finder.uses) {
        auto range = fileRangeOf(context, use->getSourceRange());
        if (!range) {
            return makeRefusal(codes::MacroInvolved, "The receiver parameter is used in a macro.",
                               rangeAt(context, use->getLocation()));
        }
        bool replacedByCall = false;
        for (const auto &call : receiverParameterRangesInCalls_) {
            replacedByCall = replacedByCall || (call.file == range->file && call.begin == range->begin);
        }
        if (replacedByCall || usesRewrittenInMovedReceivers_.count(FileOffset{range->file, range->begin})) {
            continue;
        }
        auto edit = editForParameterUse(context, use);
        if (!edit) {
            return makeRefusal(codes::MacroInvolved, "A member is used in a macro.",
                               rangeAt(context, use->getLocation()));
        }
        bool ok = plan_.replace(range->file, edit->offset, edit->length, edit->newText);
        if (!ok) {
            return makeRefusal(codes::UnsupportedReference,
                               "A use of the receiver parameter overlaps another edit, such as a recursive call "
                               "whose receiver is built from it.",
                               rangeAt(context, use->getLocation()));
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> ConvertToInstanceMethodPlanner::writeEdits() {
    for (const auto &reference : references_) {
        if (!reference.call || reference.inTemplateInstantiation || !reference.inWorkspace || reference.inMacro ||
            receiverIndex_ >= reference.call->getNumArgs()) {
            continue;
        }
        if (const auto *member = dyn_cast<MemberExpr>(reference.expression); member && !member->isImplicitAccess()) {
            continue;
        }
        if (auto refusal = rewriteCall(reference)) {
            return refusal;
        }
    }
    for (const auto *declaration : declarations_) {
        if (auto refusal = rewriteDeclaration(declaration)) {
            return refusal;
        }
    }
    for (const auto *declaration : declarations_) {
        if (declaration->doesThisDeclarationHaveABody()) {
            if (auto refusal = rewriteBody(declaration)) {
                return refusal;
            }
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> ConvertToInstanceMethodPlanner::verify() {
    return verifyEditPlan(workspace_, loaded_, plan_, {}, "making the method an instance method");
}

RefactoringResult convertToInstanceMethod(const Workspace &workspace, const ConvertToInstanceMethodRequest &request) {
    auto refuse = [&](RefusalReason reason) { return RefactoringResult::refused(workspace.root(), std::move(reason)); };
    ConvertToInstanceMethodPlanner planner(workspace, request);
    if (auto refusal = planner.parseAndCollect()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.findMethodAndReceiver()) {
        return refuse(*refusal);
    }
    planner.checkPreconditions();
    if (planner.firstBrokenPrecondition()) {
        return refuse(*planner.firstBrokenPrecondition());
    }
    if (auto refusal = planner.writeEdits()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.verify()) {
        return refuse(*refusal);
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.plan().edits()));
}

} // namespace riktig
