#include "MakeMethodStatic.h"

#include "Independence.h"
#include "NameChecks.h"
#include "RawTokens.h"
#include "RefusalCodes.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

namespace riktig {

using namespace clang;
namespace codes = refusal_codes;

namespace {

bool isWhitespace(llvm::StringRef text) {
    return llvm::all_of(text, [](char character) { return std::isspace(static_cast<unsigned char>(character)); });
}

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

bool isThisOrDereferencedThis(const Expr *expression) {
    expression = expression->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(expression)) {
        return true;
    }
    if (const auto *unary = dyn_cast<UnaryOperator>(expression); unary && unary->getOpcode() == UO_Deref) {
        return isa<CXXThisExpr>(unary->getSubExpr()->IgnoreParenImpCasts());
    }
    return false;
}

class LambdaCaptureOfThisFinder : public RecursiveASTVisitor<LambdaCaptureOfThisFinder> {
  public:
    bool VisitLambdaExpr(LambdaExpr *lambda) {
        for (const auto &capture : lambda->captures()) {
            if (capture.capturesThis()) {
                capturingByReference.push_back(lambda);
            }
            if (capture.capturesThis() &&
                (capture.isExplicit() || capture.getCaptureKind() != LCK_This ||
                 lambda->getCaptureDefault() != LCD_ByRef) &&
                !found) {
                found = lambda;
            }
        }
        return true;
    }
    // A local class's members have their own `this`.
    bool TraverseCXXRecordDecl(CXXRecordDecl *record) { return true; }
    const LambdaExpr *found = nullptr;
    std::vector<const LambdaExpr *> capturingByReference;
};

// Uses of the receiver in the method's body: implicit member accesses and `this`.
class ReceiverUseFinder : public RecursiveASTVisitor<ReceiverUseFinder> {
  public:
    explicit ReceiverUseFinder(ASTContext &context, const CXXMethodDecl *method) : context_(context), method_(method) {}

    // A local class's members have their own `this`.
    bool TraverseCXXRecordDecl(CXXRecordDecl *record) { return true; }

    bool VisitMemberExpr(MemberExpr *member) {
        if (member->isImplicitAccess() && !isTarget(member->getMemberDecl())) {
            implicitAccesses.push_back(member);
        }
        return true;
    }

    bool VisitCXXThisExpr(CXXThisExpr *expression) {
        if (expression->isImplicit()) {
            return true;
        }
        const auto *parent = parentIgnoringParensAndImplicitCasts(context_, expression);
        if (const auto *member = dyn_cast_or_null<MemberExpr>(parent);
            member && member->getBase()->IgnoreParenImpCasts() == expression && isTarget(member->getMemberDecl())) {
            return true;
        }
        explicitThis.push_back(expression);
        return true;
    }

    std::vector<const MemberExpr *> implicitAccesses;
    std::vector<const CXXThisExpr *> explicitThis;

  private:
    bool isTarget(const ValueDecl *declaration) const {
        const auto *function = dyn_cast<FunctionDecl>(declaration);
        return function && function->getCanonicalDecl() == method_->getCanonicalDecl();
    }

    ASTContext &context_;
    const CXXMethodDecl *method_;
};

} // namespace

MakeMethodStaticPlanner::MakeMethodStaticPlanner(const Workspace &workspace, MakeMethodStaticRequest request)
    : workspace_(workspace), request_(std::move(request)), plan_(workspace) {}

std::optional<FileRange> MakeMethodStaticPlanner::rangeAt(const ASTContext &context, SourceLocation location) const {
    auto offset = fileOffsetOf(context.getSourceManager(),
                               context.getSourceManager().getExpansionLoc(location.isValid() ? location : location));
    if (!offset) {
        return std::nullopt;
    }
    return rangeOf(workspace_, *offset);
}

void MakeMethodStaticPlanner::breakPrecondition(const char *code, std::string message, const ASTContext *context,
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

std::optional<RefusalReason> MakeMethodStaticPlanner::parseAndCollect() { return loadWorkspace(workspace_, loaded_); }

std::optional<RefusalReason> MakeMethodStaticPlanner::findMethod() {
    const NamedDecl *declaration = nullptr;
    if (auto refusal = findDeclarationAtCursor(workspace_, loaded_, request_.file, request_.at, declaration)) {
        return refusal;
    }
    if (isa<FunctionTemplateDecl>(declaration)) {
        return makeRefusal(codes::TemplateInvolved, "The tool does not make member function templates static.",
                           rangeAt(declaration->getASTContext(), declaration->getLocation()));
    }
    const auto *method = dyn_cast<CXXMethodDecl>(declaration);
    if (!method || method->isStatic() || isa<CXXConstructorDecl, CXXDestructorDecl, CXXConversionDecl>(method) ||
        !method->getDeclName().isIdentifier()) {
        return makeRefusal(codes::NotAnInstanceMethod,
                           "`" + declaration->getNameAsString() +
                               "` is not an instance method named by an identifier (a constructor, destructor, "
                               "operator or static member can't be made static).",
                           rangeAt(declaration->getASTContext(), declaration->getLocation()));
    }
    method_ = cast<CXXMethodDecl>(method->getCanonicalDecl());
    record_ = method_->getParent();
    declarations_ = declarationsOf(loaded_, method_);
    references_ = findFunctionReferences(workspace_, loaded_, method_);
    return std::nullopt;
}

bool MakeMethodStaticPlanner::isInBodyOfMethod(const FunctionReference &reference) const {
    auto location = fileOffsetOf(
        reference.context->getSourceManager(),
        reference.context->getSourceManager().getExpansionLoc(cast<MemberExpr>(reference.expression)->getMemberLoc()));
    if (!location) {
        return false;
    }
    for (const auto *declaration : declarations_) {
        if (!declaration->doesThisDeclarationHaveABody()) {
            continue;
        }
        auto body = fileRangeOf(declaration->getASTContext(), declaration->getBody()->getSourceRange());
        if (body && body->file == location->file && body->begin <= location->offset && location->offset < body->end) {
            return true;
        }
    }
    return false;
}

void MakeMethodStaticPlanner::checkPreconditions() {
    const auto &context = method_->getASTContext();
    auto location = method_->getLocation();
    auto name = method_->getNameAsString();
    if (request_.pass != "receiver") {
        breakPrecondition(codes::UnsupportedOption,
                          "Only `pass: \"receiver\"` is supported; passing members is not implemented yet.");
    }
    if (auto refusal = validateNewName("", request_.receiverName)) {
        brokenPrecondition_ = brokenPrecondition_ ? brokenPrecondition_ : refusal;
    }
    auto identity = identityOf(method_);
    if (identity.location.file.empty() || !workspace_.contains(identity.location.file)) {
        breakPrecondition(codes::DeclaredOutsideWorkspace, "`" + name + "` is declared outside the workspace.");
    }
    if (identity.tag.find('@') != std::string::npos) {
        breakPrecondition(codes::MacroInvolved, "`" + name + "` is declared by a macro.", &context, location);
    }
    if (method_->isVirtual() || method_->size_overridden_methods() > 0) {
        breakPrecondition(codes::VirtualDispatch,
                          "`" + name + "` is virtual, so calls to it are dispatched on the receiver's dynamic type.",
                          &context, location);
    }
    if (record_->isDependentContext() || isa<ClassTemplateSpecializationDecl>(record_) ||
        method_->getTemplatedKind() != FunctionDecl::TK_NonTemplate) {
        breakPrecondition(codes::TemplateInvolved, "`" + name + "` is a member of a template.", &context, location);
    }
    if (record_->isLambda() || !record_->getIdentifier()) {
        breakPrecondition(codes::UnsupportedMethod, "`" + name + "` is a member of an unnamed class.", &context,
                          location);
    }
    if (method_->getMethodQualifiers().hasVolatile() || method_->getRefQualifier() != RQ_None) {
        breakPrecondition(codes::UnsupportedMethod,
                          "`" + name + "` is `volatile` or ref-qualified, which a static member function can't be.",
                          &context, location);
    }
    if (method_->isDeleted() || method_->isExplicitlyDefaulted()) {
        breakPrecondition(codes::UnsupportedMethod, "`" + name + "` is deleted or defaulted.", &context, location);
    }
    for (const auto &unit : loaded_.units) {
        if (unit.macroNames.count(request_.receiverName) || unit.macroNames.count(name)) {
            breakPrecondition(codes::MacroInvolved,
                              "`" + request_.receiverName + "` or `" + name + "` is also the name of a macro.");
        }
    }
    bool hasClassDeclaration = false;
    for (const auto *declaration : declarations_) {
        const auto &declarationContext = declaration->getASTContext();
        if (declaration->getFriendObjectKind() != Decl::FOK_None) {
            breakPrecondition(codes::UnsupportedMethod, "A friend declaration names `" + name + "`.",
                              &declarationContext, declaration->getLocation());
        }
        hasClassDeclaration = hasClassDeclaration || declaration->getLexicalDeclContext() == declaration->getParent();
        if (auto range = fileRangeOf(declarationContext, declaration->getSourceRange())) {
            auto text = plan_.text(range->file).substr(range->begin, range->end - range->begin);
            forEachRawIdentifier(text, [&](const RawIdentifier &identifier) {
                if (identifier.text == request_.receiverName) {
                    breakPrecondition(codes::NameCollision,
                                      "`" + request_.receiverName + "` is already used in `" + name +
                                          "`, so the new parameter would hide or collide with it.",
                                      &declarationContext, declaration->getLocation(), "Choose another receiver name.");
                }
            });
        } else {
            breakPrecondition(codes::MacroInvolved, "A declaration of `" + name + "` is written in a macro.",
                              &declarationContext, declaration->getLocation());
        }
        if (declaration->doesThisDeclarationHaveABody()) {
            LambdaCaptureOfThisFinder lambdas;
            lambdas.TraverseStmt(declaration->getBody());
            for (const auto *lambda : lambdas.capturingByReference) {
                if (auto offset = fileOffsetOfLocation(declarationContext, lambda->getCaptureDefaultLoc())) {
                    implicitCaptureDefaults_.push_back(*offset);
                }
                if (auto range = fileRangeOf(declarationContext, lambda->getSourceRange())) {
                    lambdasCapturingThis_.push_back(*range);
                }
                llvm::DenseMap<const ValueDecl *, FieldDecl *> captureFields;
                FieldDecl *thisField = nullptr;
                lambda->getLambdaClass()->getCaptureFields(captureFields, thisField);
                if (thisField) {
                    if (auto offset = fileOffsetOf(declarationContext.getSourceManager(), thisField->getLocation())) {
                        closureMembersCapturingThis_.insert(*offset);
                    }
                }
            }
            if (lambdas.found) {
                breakPrecondition(codes::ReceiverCaptured,
                                  "A lambda in `" + name +
                                      "` captures `this` explicitly or by copy; after the change it would capture "
                                      "the receiver parameter instead.",
                                  &declarationContext, lambdas.found->getBeginLoc());
            }
        }
    }
    if (!hasClassDeclaration) {
        breakPrecondition(codes::InternalVerificationFailed, "The declaration of `" + name + "` was not found.");
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
            breakPrecondition(codes::TemplateInvolved,
                              "A template refers to `" + name +
                                  "`, and its other instantiations may call another "
                                  "function.",
                              &referenceContext, at);
            continue;
        }
        const auto *member = dyn_cast<MemberExpr>(reference.expression);
        if (!member || !reference.call) {
            breakPrecondition(codes::MemberFunctionPointer,
                              "`" + name +
                                  "` is used other than by calling it, such as taking a pointer to it, "
                                  "whose type would change.",
                              &referenceContext, at);
            continue;
        }
        if (member->isImplicitAccess()) {
            continue;
        }
        const auto *base = member->getBase();
        if (member->isArrow() && isa<CXXOperatorCallExpr>(base->IgnoreImpCasts())) {
            breakPrecondition(codes::UnsupportedReference,
                              "`" + name + "` is called through an overloaded `operator->`.", &referenceContext, at);
            continue;
        }
        if (!method_->isConst() && !member->isArrow() && !base->isLValue()) {
            breakPrecondition(codes::ReceiverNotLvalue,
                              "A call to the non-`const` `" + name +
                                  "` has a temporary receiver, which a non-`const` reference parameter can't bind.",
                              &referenceContext, base->getBeginLoc());
            continue;
        }
        IndependenceAnalysis independence(*reference.context, reference.enclosingFunction);
        for (const auto *argument : writtenArguments(reference.call)) {
            if (!independence.areIndependent(base, argument)) {
                breakPrecondition(codes::EvaluationOrder,
                                  "The receiver is evaluated before the arguments now, but in any order after the "
                                  "change, and the tool can't show the receiver and this argument are independent.",
                                  &referenceContext, argument->getBeginLoc(),
                                  "Store the receiver or the argument in a local variable first.");
                break;
            }
        }
    }
}

std::optional<RefusalReason> MakeMethodStaticPlanner::rewriteCall(const FunctionReference &reference) {
    const auto *member = cast<MemberExpr>(reference.expression);
    const auto &context = *reference.context;
    auto failed = [&](std::string message) {
        return makeRefusal(codes::UnsupportedReference, std::move(message), rangeAt(context, member->getMemberLoc()));
    };
    auto name = fileOffsetOfLocation(context, member->getMemberLoc());
    auto lparen = nextTokenAfter(context, member->getEndLoc());
    if (!name || !lparen || textOf(plan_, *lparen) != "(") {
        return failed("The call to `" + method_->getNameAsString() + "` is not written as `name(...)`.");
    }
    auto qualifier = qualifierFrom(record_, reference.semanticContext);
    if (!qualifier || qualifier->empty()) {
        return failed("The tool can't name the class from this call.");
    }
    auto qualifierText = spell(*qualifier) + "::";
    bool hasArguments = !writtenArguments(reference.call).empty();
    bool inBody = isInBodyOfMethod(reference);
    const auto &file = name->file;

    std::string receiverText;
    std::optional<OffsetRange> movedReceiver;
    unsigned receiverPrefix = 0;
    unsigned qualifierOffset = name->offset;
    if (member->isImplicitAccess()) {
        receiverText = inBody ? request_.receiverName : "*this";
        if (auto qualifierLoc = member->getQualifierLoc()) {
            auto begin = fileOffsetOfLocation(context, qualifierLoc.getBeginLoc());
            if (!begin || begin->file != file) {
                return failed("The call's qualifier is written in a macro.");
            }
            qualifierOffset = begin->offset;
        }
    } else {
        auto base = fileRangeOf(context, member->getBase()->getSourceRange());
        if (!base || base->file != file || base->end > name->offset) {
            return failed("The call's receiver is written in a macro.");
        }
        qualifierOffset = base->begin;
        if (inBody && isThisOrDereferencedThis(member->getBase())) {
            receiverText = request_.receiverName;
            receiversInBody_.push_back({*base, false});
        } else {
            receiverPrefix = member->isArrow() ? 1 : 0;
            receiverText = (member->isArrow() ? "*" : "") + textOf(plan_, *base);
            movedReceiver = base;
            if (inBody) {
                receiversInBody_.push_back({*base, true});
            }
        }
    }
    auto argumentSeparator = hasArguments ? ", " : "";
    if (!plan_.replace(file, qualifierOffset, name->offset - qualifierOffset, qualifierText) ||
        !plan_.insert(file, lparen->end, receiverText + argumentSeparator)) {
        return failed("The call to `" + method_->getNameAsString() +
                      "` overlaps another call that changes, such as "
                      "a call in its receiver.");
    }
    plan_.expectQualifier(file, qualifierOffset, name->offset - qualifierOffset, 0, *qualifier);
    if (movedReceiver) {
        plan_.relocate(file, movedReceiver->begin, movedReceiver->end - movedReceiver->begin, file, lparen->end, 0,
                       receiverPrefix);
    }
    if (inBody && !movedReceiver) {
        receiverUsesInBody_.push_back({file, lparen->end, 0, 0});
    }
    return std::nullopt;
}

std::optional<RefusalReason> MakeMethodStaticPlanner::rewriteDeclaration(const CXXMethodDecl *declaration) {
    const auto &context = declaration->getASTContext();
    auto failed = [&](std::string message) {
        return makeRefusal(codes::UnsupportedMethod, std::move(message), rangeAt(context, declaration->getLocation()));
    };
    auto typeLoc = declaration->getFunctionTypeLoc();
    if (!typeLoc) {
        return failed("The declaration's parameter list is not written out.");
    }
    auto lparen = fileOffsetOfLocation(context, typeLoc.getLParenLoc());
    auto rparen = fileOffsetOfLocation(context, typeLoc.getRParenLoc());
    if (!lparen || !rparen || lparen->file != rparen->file) {
        return failed("The declaration's parameter list is written in a macro.");
    }
    const auto &file = lparen->file;
    auto className = record_->getName().str();
    std::string parameter = std::string(declaration->isConst() ? "const " : "") + className + " &";
    unsigned typePosition = declaration->isConst() ? 6 : 0;
    unsigned namePosition = parameter.size();
    parameter += request_.receiverName;
    unsigned replacementOffset = lparen->offset + 1;
    unsigned replacementLength = 0;
    if (declaration->getNumParams() == 0) {
        replacementLength = rparen->offset - replacementOffset;
    } else {
        parameter += ", ";
    }
    if (!plan_.replace(file, replacementOffset, replacementLength, parameter)) {
        return failed("The declaration's parameter list overlaps another edit.");
    }
    plan_.expectBinding(file, replacementOffset, replacementLength, typePosition, "type", identityOf(record_));
    parameterNames_.push_back({file, replacementOffset, replacementLength, namePosition});
    if (declaration->doesThisDeclarationHaveABody()) {
        definitionParameter_ = parameterNames_.back();
    }

    if (declaration->isConst()) {
        auto constToken = nextTokenAfter(context, typeLoc.getRParenLoc());
        if (!constToken || textOf(plan_, *constToken) != "const") {
            return failed("The `const` qualifier is not written right after the parameter list.");
        }
        unsigned removeFrom = constToken->begin;
        if (isWhitespace(llvm::StringRef(plan_.text(file)).slice(rparen->offset + 1, constToken->begin))) {
            removeFrom = rparen->offset + 1;
        }
        if (!plan_.replace(file, removeFrom, constToken->end - removeFrom, "")) {
            return failed("The `const` qualifier overlaps another edit.");
        }
    }
    if (declaration->getLexicalDeclContext() == declaration->getParent()) {
        auto begin = fileOffsetOfLocation(context, declaration->getBeginLoc());
        if (!begin || begin->file != file) {
            return failed("The declaration's start is written in a macro.");
        }
        auto start = llvm::StringRef(plan_.text(file)).substr(begin->offset);
        if (start.starts_with("[[") || start.starts_with("__attribute") || start.starts_with("alignas")) {
            return failed("The declaration starts with an attribute, before which `static` can't be written.");
        }
        if (!plan_.insert(file, begin->offset, "static ")) {
            return failed("The declaration overlaps another edit.");
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> MakeMethodStaticPlanner::rewriteBody(const CXXMethodDecl *definition) {
    auto &context = definition->getASTContext();
    ReceiverUseFinder finder(context, method_);
    finder.TraverseStmt(definition->getBody());
    auto failed = [&](std::string message, SourceLocation location) {
        return makeRefusal(codes::UnsupportedReference, std::move(message), rangeAt(context, location));
    };
    // Whether the edit is inside the receiver of a recursive call, which that call's rewrite replaced or moves.
    auto isInRecursiveCallReceiver = [&](const std::string &file, unsigned offset, bool &moves) {
        for (const auto &receiver : receiversInBody_) {
            if (receiver.range.file == file && receiver.range.begin <= offset && offset < receiver.range.end) {
                moves = receiver.moves;
                return true;
            }
        }
        return false;
    };
    auto addReceiverUse = [&](const std::string &file, unsigned offset, unsigned length, std::string text,
                              unsigned receiverPosition, SourceLocation location) -> std::optional<RefusalReason> {
        bool moves = false;
        if (isInRecursiveCallReceiver(file, offset, moves)) {
            if (moves) {
                return failed("A recursive call's receiver uses the object's members.", location);
            }
            return std::nullopt;
        }
        if (!plan_.replace(file, offset, length, std::move(text))) {
            return failed("A use of `this` overlaps another edit.", location);
        }
        receiverUsesInBody_.push_back({file, offset, length, receiverPosition});
        return std::nullopt;
    };
    for (const auto *member : finder.implicitAccesses) {
        auto location = member->getQualifierLoc() ? member->getQualifierLoc().getBeginLoc() : member->getMemberLoc();
        auto offset = fileOffsetOfLocation(context, location);
        if (!offset) {
            return failed("A member is used in a macro.", member->getMemberLoc());
        }
        if (auto refusal = addReceiverUse(offset->file, offset->offset, 0, request_.receiverName + ".", 0, location)) {
            return refusal;
        }
    }
    for (const auto *thisExpression : finder.explicitThis) {
        auto range = fileRangeOf(context, thisExpression->getSourceRange());
        if (!range) {
            return failed("`this` is used in a macro.", thisExpression->getLocation());
        }
        const auto *parent = parentIgnoringParensAndImplicitCasts(context, thisExpression);
        if (const auto *member = dyn_cast_or_null<MemberExpr>(parent);
            member && member->isArrow() && member->getBase()->IgnoreParenImpCasts() == thisExpression) {
            auto location =
                member->getQualifierLoc() ? member->getQualifierLoc().getBeginLoc() : member->getMemberLoc();
            auto memberOffset = fileOffsetOfLocation(context, location);
            if (!memberOffset || memberOffset->file != range->file) {
                return failed("A member is used in a macro.", member->getMemberLoc());
            }
            if (auto refusal = addReceiverUse(range->file, range->begin, memberOffset->offset - range->begin,
                                              request_.receiverName + ".", 0, thisExpression->getLocation())) {
                return refusal;
            }
            continue;
        }
        if (const auto *unary = dyn_cast_or_null<UnaryOperator>(parent); unary && unary->getOpcode() == UO_Deref) {
            auto unaryRange = fileRangeOf(context, unary->getSourceRange());
            if (!unaryRange) {
                return failed("`*this` is written in a macro.", unary->getOperatorLoc());
            }
            if (auto refusal = addReceiverUse(unaryRange->file, unaryRange->begin, unaryRange->end - unaryRange->begin,
                                              request_.receiverName, 0, thisExpression->getLocation())) {
                return refusal;
            }
            continue;
        }
        // A `this` initializing a variable has no statement parent.
        bool needsParentheses =
            parent && !isa<CallExpr, CXXConstructExpr, ReturnStmt, BinaryOperator, InitListExpr>(parent);
        auto text = needsParentheses ? "(&" + request_.receiverName + ")" : "&" + request_.receiverName;
        if (auto refusal = addReceiverUse(range->file, range->begin, range->end - range->begin, text,
                                          needsParentheses ? 2 : 1, thisExpression->getLocation())) {
            return refusal;
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> MakeMethodStaticPlanner::writeEdits() {
    for (const auto &reference : references_) {
        if (!reference.call || !isa<MemberExpr>(reference.expression) || reference.inTemplateInstantiation ||
            !reference.inWorkspace || reference.inMacro) {
            continue;
        }
        if (auto refusal = rewriteCall(reference)) {
            return refusal;
        }
    }
    for (const auto *declaration : declarations_) {
        if (auto refusal = rewriteDeclaration(cast<CXXMethodDecl>(declaration))) {
            return refusal;
        }
    }
    for (const auto *declaration : declarations_) {
        if (declaration->doesThisDeclarationHaveABody()) {
            if (auto refusal = rewriteBody(cast<CXXMethodDecl>(declaration))) {
                return refusal;
            }
        }
    }
    for (const auto &parameter : parameterNames_) {
        auto after = plan_.afterOffset(parameter.file, parameter.replacementOffset, parameter.replacementLength,
                                       parameter.positionInNewText);
        plan_.expectBinding(parameter.file, parameter.replacementOffset, parameter.replacementLength,
                            parameter.positionInNewText, "decl", DeclIdentity{{parameter.file, after}, "ParmVar"},
                            /*identityIsAfter=*/true);
    }
    if (definitionParameter_) {
        const auto &parameter = *definitionParameter_;
        DeclIdentity receiver{
            {parameter.file, plan_.afterOffset(parameter.file, parameter.replacementOffset, parameter.replacementLength,
                                               parameter.positionInNewText)},
            "ParmVar"};
        for (const auto &use : receiverUsesInBody_) {
            plan_.expectBinding(use.file, use.replacementOffset, use.replacementLength, use.positionInNewText, "ref",
                                receiver, /*identityIsAfter=*/true);
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> MakeMethodStaticPlanner::verify() {
    // A `[&]` lambda that captured `this` now captures the receiver parameter: the binding records it at the
    // capture default, and the closure's implicit member for it is declared at its first use.
    BindingExpectations intended;
    intended.isNew = [&](const BindingKey &key) {
        if (key.role == "decl" || key.role == "type") {
            for (const auto &lambda : lambdasCapturingThis_) {
                if (key.location.file == lambda.file &&
                    plan_.edits().mapOffset(lambda.file, lambda.begin) <= key.location.offset &&
                    key.location.offset < plan_.edits().mapOffset(lambda.file, lambda.end)) {
                    return true;
                }
            }
        }
        for (const auto &captureDefault : implicitCaptureDefaults_) {
            if (plan_.edits().map(captureDefault) == key.location) {
                return true;
            }
        }
        return false;
    };
    intended.isDropped = [&](const BindingKey &key, const DeclIdentity &identity) {
        return (key.role == "decl" || key.role == "type") && closureMembersCapturingThis_.count(key.location) > 0;
    };
    return verifyEditPlan(workspace_, loaded_, plan_, intended, "making the method static");
}

RefactoringResult makeMethodStatic(const Workspace &workspace, const MakeMethodStaticRequest &request) {
    auto refuse = [&](RefusalReason reason) { return RefactoringResult::refused(workspace.root(), std::move(reason)); };
    MakeMethodStaticPlanner planner(workspace, request);
    if (auto refusal = planner.parseAndCollect()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.findMethod()) {
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
