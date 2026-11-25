#include "MoveStaticMember.h"

#include "NameChecks.h"
#include "RefusalCodes.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

#include <set>

namespace riktig {

using namespace clang;
namespace codes = refusal_codes;

namespace {

std::string qualifiedNameOf(const DeclContext *context) {
    if (const auto *named = dyn_cast<NamedDecl>(context)) {
        return named->getQualifiedNameAsString();
    }
    return {};
}

// The qualified names of a context and the contexts enclosing it, without the translation unit.
std::set<std::string> enclosingNames(const DeclContext *context) {
    std::set<std::string> names;
    for (; context && !context->isTranslationUnit(); context = context->getParent()) {
        if (isa<NamespaceDecl, CXXRecordDecl>(context)) {
            names.insert(qualifiedNameOf(context));
        }
    }
    return names;
}

// Evaluating the expression has no effect, so a reference `expression.member` can drop it.
bool isDiscardable(const Expr *expression) {
    expression = expression->IgnoreParenImpCasts();
    if (expression->getType().isVolatileQualified()) {
        return false;
    }
    if (isa<CXXThisExpr>(expression)) {
        return true;
    }
    if (const auto *reference = dyn_cast<DeclRefExpr>(expression)) {
        return isa<VarDecl>(reference->getDecl());
    }
    if (const auto *unary = dyn_cast<UnaryOperator>(expression); unary && unary->getOpcode() == UO_Deref) {
        return isDiscardable(unary->getSubExpr());
    }
    if (const auto *member = dyn_cast<MemberExpr>(expression); member && isa<FieldDecl>(member->getMemberDecl())) {
        return isDiscardable(member->getBase());
    }
    return false;
}

std::string accessSpelling(AccessSpecifier access) {
    switch (access) {
    case AS_public:
        return "public";
    case AS_protected:
        return "protected";
    case AS_private:
        return "private";
    default:
        return "public";
    }
}

class SourceScopeNameFinder : public RecursiveASTVisitor<SourceScopeNameFinder> {
  public:
    bool shouldVisitImplicitCode() const { return false; }

    bool VisitDeclRefExpr(DeclRefExpr *reference) {
        if (!reference->hasQualifier()) {
            names.push_back({reference->getLocation(), reference->getFoundDecl()});
        }
        return true;
    }
    bool VisitTagTypeLoc(TagTypeLoc type) {
        names.push_back({type.getNameLoc(), type.getDecl()});
        return true;
    }
    bool VisitTypedefTypeLoc(TypedefTypeLoc type) {
        names.push_back({type.getNameLoc(), type.getTypePtr()->getDecl()});
        return true;
    }
    bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc specifier) {
        // Only the first component of a qualifier is looked up unqualified.
        if (specifier && !prefixOf(specifier)) {
            auto nested = specifier.getNestedNameSpecifier();
            if (nested.getKind() == NestedNameSpecifier::Kind::Namespace) {
                if (const auto *ns = dyn_cast<NamespaceDecl>(nested.getAsNamespaceAndPrefix().Namespace)) {
                    names.push_back({specifier.getLocalBeginLoc(), ns});
                }
            } else if (nested.getKind() == NestedNameSpecifier::Kind::Type) {
                const auto *type = nested.getAsType();
                if (const auto *tag = type->getAsTagDecl()) {
                    names.push_back({specifier.getLocalBeginLoc(), tag});
                } else if (const auto *typedefType = type->getAs<TypedefType>()) {
                    names.push_back({specifier.getLocalBeginLoc(), typedefType->getDecl()});
                }
            }
            return true;
        }
        return RecursiveASTVisitor::TraverseNestedNameSpecifierLoc(specifier);
    }

    static NestedNameSpecifierLoc prefixOf(NestedNameSpecifierLoc specifier) {
        switch (specifier.getNestedNameSpecifier().getKind()) {
        case NestedNameSpecifier::Kind::Namespace:
            return specifier.castAsNamespaceAndPrefix().Prefix;
        case NestedNameSpecifier::Kind::Type:
            return specifier.castAsTypeLoc().getPrefix();
        default:
            return {};
        }
    }

    struct Name {
        SourceLocation location;
        const NamedDecl *declaration;
    };
    std::vector<Name> names;
};

} // namespace

MoveStaticMemberPlanner::MoveStaticMemberPlanner(const Workspace &workspace, MoveStaticMemberRequest request)
    : workspace_(workspace), request_(std::move(request)), plan_(workspace) {}

std::optional<FileRange> MoveStaticMemberPlanner::rangeAt(const ASTContext &context, SourceLocation location) const {
    auto offset = fileOffsetOf(context.getSourceManager(), context.getSourceManager().getExpansionLoc(location));
    if (!offset) {
        return std::nullopt;
    }
    return rangeOf(workspace_, *offset);
}

void MoveStaticMemberPlanner::breakPrecondition(const char *code, std::string message, const ASTContext *context,
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

std::optional<RefusalReason> MoveStaticMemberPlanner::parseAndCollect() { return loadWorkspace(workspace_, loaded_); }

const DeclContext *MoveStaticMemberPlanner::targetIn(const ASTContext &context) const {
    const DeclContext *current = context.getTranslationUnitDecl();
    for (const auto &name : targetPath_) {
        const DeclContext *next = nullptr;
        auto &identifiers = const_cast<ASTContext &>(context).Idents;
        for (const auto *found : current->getPrimaryContext()->lookup(DeclarationName(&identifiers.get(name)))) {
            if (const auto *ns = dyn_cast<NamespaceDecl>(found)) {
                next = ns;
            } else if (const auto *record = dyn_cast<CXXRecordDecl>(found); record && record->getDefinition()) {
                next = record->getDefinition();
            }
        }
        if (!next) {
            return nullptr;
        }
        current = next;
    }
    return current == context.getTranslationUnitDecl() ? nullptr : current;
}

const ValueDecl *MoveStaticMemberPlanner::movingDeclaration() const {
    for (const auto *declaration : declarations_) {
        if (!memberOfClass_ || declaration->getLexicalDeclContext() == declaration->getDeclContext()) {
            return declaration;
        }
    }
    return nullptr;
}

std::optional<RefusalReason> MoveStaticMemberPlanner::findMemberAndTarget() {
    const NamedDecl *declaration = nullptr;
    if (auto refusal = findDeclarationAtCursor(workspace_, loaded_, request_.file, request_.at, declaration)) {
        return refusal;
    }
    const auto &context = declaration->getASTContext();
    if (isa<FunctionTemplateDecl, VarTemplateDecl>(declaration)) {
        return makeRefusal(codes::TemplateInvolved, "The tool does not move templates.",
                           rangeAt(context, declaration->getLocation()));
    }
    const auto *value = dyn_cast<ValueDecl>(declaration);
    const auto *method = dyn_cast_or_null<CXXMethodDecl>(value);
    const auto *function = dyn_cast_or_null<FunctionDecl>(value);
    const auto *variable = dyn_cast_or_null<VarDecl>(value);
    bool namespaceScope = value && value->getDeclContext()->getRedeclContext()->isFileContext();
    if (method && method->isStatic() && method->getDeclName().isIdentifier()) {
        memberOfClass_ = true;
    } else if (variable && variable->isStaticDataMember()) {
        memberOfClass_ = true;
    } else if (((function && !method && function->getDeclName().isIdentifier()) ||
                (variable && variable->hasGlobalStorage())) &&
               namespaceScope) {
        memberOfClass_ = false;
    } else {
        return makeRefusal(codes::NotAStaticMember,
                           "`" + declaration->getNameAsString() +
                               "` is not a static member function or data member, or a namespace-scope function "
                               "or variable.",
                           rangeAt(context, declaration->getLocation()), "Make an instance method static first.");
    }
    member_ = cast<ValueDecl>(value->getCanonicalDecl());
    declarations_ = declarationsOf(loaded_, member_);
    references_ = findReferences(workspace_, loaded_, member_);

    llvm::StringRef target = request_.target;
    target.consume_front("::");
    llvm::SmallVector<llvm::StringRef> parts;
    target.split(parts, "::");
    for (auto part : parts) {
        if (!isValidIdentifier(part)) {
            return makeRefusal(codes::InvalidTarget, "`" + request_.target + "` is not a qualified name.");
        }
        targetPath_.push_back(part.str());
    }
    const auto *moving = movingDeclaration();
    if (!moving) {
        return makeRefusal(codes::InternalVerificationFailed, "The declaration that would move was not found.");
    }
    target_ = targetIn(moving->getASTContext());
    if (!target_) {
        return makeRefusal(codes::InvalidTarget, "No class or namespace named `" + request_.target +
                                                     "` is declared where `" + member_->getNameAsString() +
                                                     "` is declared.");
    }
    return std::nullopt;
}

std::optional<OffsetRange> MoveStaticMemberPlanner::movingRange() const {
    const auto *moving = movingDeclaration();
    const auto &context = moving->getASTContext();
    auto range = fileRangeOf(context, moving->getSourceRange());
    if (!range) {
        return std::nullopt;
    }
    const auto *function = dyn_cast<FunctionDecl>(moving);
    if (!function || !function->doesThisDeclarationHaveABody()) {
        auto semicolon = nextTokenAfter(context, moving->getEndLoc());
        if (!semicolon || semicolon->file != range->file ||
            const_cast<EditPlan &>(plan_).text(range->file).substr(semicolon->begin, 1) != ";") {
            return std::nullopt;
        }
        range->end = semicolon->end;
    }
    const auto &text = const_cast<EditPlan &>(plan_).text(range->file);
    unsigned lineStart = range->begin;
    while (lineStart > 0 && (text[lineStart - 1] == ' ' || text[lineStart - 1] == '\t')) {
        --lineStart;
    }
    unsigned lineEnd = range->end;
    while (lineEnd < text.size() && (text[lineEnd] == ' ' || text[lineEnd] == '\t')) {
        ++lineEnd;
    }
    if ((lineStart == 0 || text[lineStart - 1] == '\n') && lineEnd < text.size() && text[lineEnd] == '\n') {
        range->begin = lineStart;
        range->end = lineEnd + 1;
    }
    return range;
}

bool MoveStaticMemberPlanner::isInMovingRange(const std::string &file, unsigned offset) const {
    auto range = movingRange();
    return range && range->file == file && range->begin <= offset && offset < range->end;
}

void MoveStaticMemberPlanner::checkPreconditions() {
    const auto *moving = movingDeclaration();
    const auto &context = moving->getASTContext();
    auto name = member_->getNameAsString();
    auto location = moving->getLocation();
    const auto *source = moving->getDeclContext()->getRedeclContext();
    auto identity = identityOf(member_);
    if (identity.location.file.empty() || !workspace_.contains(identity.location.file)) {
        breakPrecondition(codes::DeclaredOutsideWorkspace, "`" + name + "` is declared outside the workspace.");
    }
    if (identity.tag.find('@') != std::string::npos) {
        breakPrecondition(codes::MacroInvolved, "`" + name + "` is declared by a macro.", &context, location);
    }
    bool targetIsClass = isa<CXXRecordDecl>(target_);
    if (memberOfClass_ != targetIsClass) {
        breakPrecondition(codes::UnsupportedTarget,
                          "The tool moves members of a class to another class, and namespace-scope declarations "
                          "to another namespace, but not between a class and a namespace.");
    }
    if (qualifiedNameOf(target_) == qualifiedNameOf(source)) {
        breakPrecondition(codes::InvalidTarget, "`" + name + "` is already in `" + request_.target + "`.");
    }
    if (const auto *targetDecl = dyn_cast<Decl>(target_)) {
        auto targetIdentity = identityOf(cast<NamedDecl>(targetDecl));
        if (targetIdentity.location.file.empty() || !workspace_.contains(targetIdentity.location.file)) {
            breakPrecondition(codes::InvalidTarget, "`" + request_.target + "` is declared outside the workspace.");
        }
    }
    if (target_->isDependentContext() || source->isDependentContext() ||
        isa<ClassTemplateSpecializationDecl>(target_) || isa<ClassTemplateSpecializationDecl>(source)) {
        breakPrecondition(codes::TemplateInvolved, "The source or the target is a template.", &context, location);
    }
    if (const auto *record = dyn_cast<CXXRecordDecl>(target_);
        record && (record->isLocalClass() || record->isLambda())) {
        breakPrecondition(codes::InvalidTarget, "`" + request_.target + "` is a local class.");
    }
    if (const auto *variable = dyn_cast<VarDecl>(member_)) {
        bool constant = false;
        for (const auto *declaration : declarations_) {
            constant = constant || cast<VarDecl>(declaration)->isConstexpr();
        }
        if (!constant) {
            breakPrecondition(codes::DynamicInitialization,
                              "Only `constexpr` variables move; when other variables are initialized depends on "
                              "where they are defined.",
                              &context, location);
        }
        if (declarations_.size() != 1) {
            breakPrecondition(codes::UnsupportedMember, "`" + name + "` is declared more than once.", &context,
                              location);
        }
        (void)variable;
    }
    if (const auto *function = dyn_cast<FunctionDecl>(member_)) {
        if (function->isMain() || function->isExternC() || function->isDeleted()) {
            breakPrecondition(codes::UnsupportedMember, "`" + name + "` is `main`, has C linkage, or is deleted.",
                              &context, location);
        }
        if (!memberOfClass_ && declarations_.size() != 1) {
            breakPrecondition(codes::UnsupportedMember,
                              "`" + name +
                                  "` is declared more than once; the tool moves a namespace-scope function only "
                                  "when its definition is its only declaration.",
                              &context, location);
        }
    }
    for (const auto *declaration : declarations_) {
        if (declaration->getFriendObjectKind() != Decl::FOK_None) {
            breakPrecondition(codes::UnsupportedMember, "A friend declaration names `" + name + "`.",
                              &declaration->getASTContext(), declaration->getLocation());
        }
    }
    if (!movingRange()) {
        breakPrecondition(codes::MacroInvolved, "The declaration of `" + name + "` is written in a macro.", &context,
                          location);
    }

    for (const auto &unit : loaded_.units) {
        if (unit.macroNames.count(name)) {
            breakPrecondition(codes::MacroInvolved, "`" + name + "` is also the name of a macro.");
        }
        for (const auto &[key, identities] : unit.bindings) {
            if (key.role.starts_with("using") && identities.count(identity)) {
                breakPrecondition(codes::UnsupportedReference,
                                  "A using-declaration names `" + name + "`, and the tool does not update it.",
                                  &unit.unit->getASTContext(), {});
            }
        }
        const auto *targetInUnit = targetIn(unit.unit->getASTContext());
        if (!targetInUnit) {
            continue;
        }
        auto &identifiers = unit.unit->getASTContext().Idents;
        auto lookupName = DeclarationName(&identifiers.get(name));
        for (const auto *found : targetInUnit->getPrimaryContext()->lookup(lookupName)) {
            if (!isa<FunctionDecl, FunctionTemplateDecl>(found) || !isa<FunctionDecl>(member_)) {
                breakPrecondition(codes::NameCollision,
                                  "`" + request_.target + "` already has a member named `" + name + "`.",
                                  &unit.unit->getASTContext(), found->getLocation(), "Rename one of them first.");
            }
        }
        if (const auto *record = dyn_cast<CXXRecordDecl>(targetInUnit)) {
            record->forallBases([&](const CXXRecordDecl *base) {
                for (const auto *found : base->lookup(lookupName)) {
                    breakPrecondition(codes::NameCollision,
                                      "A base class of `" + request_.target + "` has a member named `" + name +
                                          "`, which the moved member would hide.",
                                      &unit.unit->getASTContext(), found->getLocation(), "Rename one of them first.");
                }
                return true;
            });
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
        } else if (reference.inMacro) {
            breakPrecondition(codes::MacroInvolved, "A macro refers to `" + name + "`.", &referenceContext, at);
        } else if (reference.inTemplateInstantiation) {
            breakPrecondition(codes::TemplateInvolved, "A template refers to `" + name + "`.", &referenceContext, at);
        } else if (const auto *member = dyn_cast<MemberExpr>(reference.expression);
                   member && !member->isImplicitAccess() && !isDiscardable(member->getBase())) {
            breakPrecondition(codes::DiscardedExpression,
                              "`" + name +
                                  "` is reached through an expression that may have an effect, which the qualified "
                                  "name would no longer evaluate.",
                              &referenceContext, member->getBase()->getBeginLoc(),
                              "Store the expression in a local variable first.");
        } else if (!targetIn(referenceContext)) {
            breakPrecondition(codes::InvalidTarget,
                              "`" + request_.target + "` is not declared where `" + name + "` is used.",
                              &referenceContext, at);
        }
    }
}

std::optional<RefusalReason> MoveStaticMemberPlanner::qualifyNamesFromSource(const ValueDecl *declaration, bool moves,
                                                                             std::vector<InnerEdit> &innerEdits) {
    auto &context = declaration->getASTContext();
    const auto *source = member_->getDeclContext()->getRedeclContext();
    auto sourceNames = enclosingNames(source);
    if (const auto *record = dyn_cast<CXXRecordDecl>(source); record && record->hasDefinition()) {
        record->forallBases([&](const CXXRecordDecl *base) {
            sourceNames.insert(base->getQualifiedNameAsString());
            return true;
        });
    }
    const auto *targetHere = targetIn(context);
    if (!targetHere) {
        return makeRefusal(codes::InvalidTarget, "`" + request_.target + "` is not declared here.",
                           rangeAt(context, declaration->getLocation()));
    }
    auto targetNames = enclosingNames(targetHere);
    const DeclContext *writtenIn = moves ? targetHere : declaration->getLexicalDeclContext();
    auto movingRangeHere = movingRange();

    SourceScopeNameFinder finder;
    finder.TraverseDecl(const_cast<ValueDecl *>(declaration));
    std::set<FileOffset> done;
    for (const auto &[location, named] : finder.names) {
        if (!named || isa<ParmVarDecl>(named) || named->getCanonicalDecl() == member_->getCanonicalDecl()) {
            continue;
        }
        const auto *namedContext = named->getDeclContext()->getRedeclContext();
        if (!isa<NamespaceDecl, CXXRecordDecl>(namedContext)) {
            continue;
        }
        auto contextName = qualifiedNameOf(namedContext);
        if (!sourceNames.count(contextName) || targetNames.count(contextName)) {
            continue;
        }
        auto offset = fileOffsetOfLocation(context, location);
        if (!offset) {
            return makeRefusal(codes::MacroInvolved, "A name in the moved declaration is written in a macro.",
                               rangeAt(context, location));
        }
        if (!done.insert(*offset).second) {
            continue;
        }
        auto qualifier = qualifierFrom(namedContext, writtenIn);
        if (!qualifier) {
            return makeRefusal(codes::UnsupportedReference,
                               "`" + named->getNameAsString() + "` can't be qualified from the target.",
                               rangeAt(context, location));
        }
        if (qualifier->empty()) {
            continue;
        }
        auto text = spell(*qualifier) + "::";
        if (moves) {
            if (!movingRangeHere || offset->file != movingRangeHere->file) {
                return makeRefusal(codes::MacroInvolved, "A name in the moved declaration is outside its text.",
                                   rangeAt(context, location));
            }
            innerEdits.push_back(
                InnerEdit{Replacement{offset->offset - movingRangeHere->begin, 0, text}, *qualifier, 0});
        } else {
            if (!plan_.insert(offset->file, offset->offset, text)) {
                return makeRefusal(codes::UnsupportedReference, "Two edits overlap.", rangeAt(context, location));
            }
            plan_.expectQualifier(offset->file, offset->offset, 0, 0, *qualifier);
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> MoveStaticMemberPlanner::writeEdits() {
    auto range = movingRange();
    if (!range) {
        return makeRefusal(codes::MacroInvolved, "The declaration is written in a macro.");
    }
    const auto *moving = movingDeclaration();
    auto &movingContext = moving->getASTContext();
    std::vector<InnerEdit> innerEdits;
    if (auto refusal = qualifyNamesFromSource(moving, /*moves=*/true, innerEdits)) {
        return refusal;
    }

    for (const auto &reference : references_) {
        if (!reference.inWorkspace || reference.inMacro || reference.inTemplateInstantiation) {
            continue;
        }
        const auto &context = *reference.context;
        const auto *targetHere = targetIn(context);
        if (!targetHere) {
            continue;
        }
        SourceLocation nameLocation;
        SourceLocation replaceFrom;
        bool isMember = false;
        if (const auto *declRef = dyn_cast<DeclRefExpr>(reference.expression)) {
            nameLocation = declRef->getLocation();
            if (declRef->hasQualifier()) {
                replaceFrom = declRef->getQualifierLoc().getBeginLoc();
            }
        } else {
            const auto *member = cast<MemberExpr>(reference.expression);
            nameLocation = member->getMemberLoc();
            isMember = true;
            if (member->getQualifierLoc()) {
                replaceFrom = member->getQualifierLoc().getBeginLoc();
            }
            if (!member->isImplicitAccess()) {
                replaceFrom = member->getBase()->getBeginLoc();
            }
        }
        auto name = fileOffsetOfLocation(context, nameLocation);
        if (!name) {
            continue;
        }
        unsigned from = name->offset;
        if (replaceFrom.isValid()) {
            auto begin = fileOffsetOfLocation(context, replaceFrom);
            if (!begin || begin->file != name->file) {
                return makeRefusal(codes::MacroInvolved, "A reference's qualifier is written in a macro.",
                                   rangeAt(context, nameLocation));
            }
            from = begin->offset;
        }
        bool insideMovingText = isInMovingRange(name->file, name->offset);
        auto qualifier =
            qualifierFrom(targetHere, insideMovingText ? targetIn(movingContext) : reference.semanticContext);
        if (!qualifier) {
            return makeRefusal(codes::UnsupportedReference, "The target can't be named from a reference.",
                               rangeAt(context, nameLocation));
        }
        if (qualifier->empty() && (isMember || from == name->offset)) {
            if (isMember) {
                return makeRefusal(codes::UnsupportedReference, "The target can't be named from a reference.",
                                   rangeAt(context, nameLocation));
            }
            continue;
        }
        auto text = qualifier->empty() ? std::string() : spell(*qualifier) + "::";
        if (insideMovingText) {
            innerEdits.push_back(InnerEdit{Replacement{from - range->begin, name->offset - from, text}, *qualifier, 0});
            continue;
        }
        if (!plan_.replace(name->file, from, name->offset - from, text)) {
            return makeRefusal(codes::UnsupportedReference, "Two edits overlap.", rangeAt(context, nameLocation));
        }
        plan_.expectQualifier(name->file, from, name->offset - from, 0, *qualifier);
    }

    // An out-of-line definition stays where it is, with the target's name as its qualifier.
    for (const auto *declaration : declarations_) {
        if (declaration == moving) {
            continue;
        }
        const auto &context = declaration->getASTContext();
        const auto *function = dyn_cast<FunctionDecl>(declaration);
        if (!function || !function->getQualifierLoc()) {
            return makeRefusal(codes::UnsupportedMember, "Another declaration of the member is not a definition.",
                               rangeAt(context, declaration->getLocation()));
        }
        auto begin = fileOffsetOfLocation(context, function->getQualifierLoc().getBeginLoc());
        auto name = fileOffsetOfLocation(context, function->getLocation());
        const auto *targetHere = targetIn(context);
        if (!begin || !name || begin->file != name->file || !targetHere) {
            return makeRefusal(codes::MacroInvolved, "The definition's name is written in a macro.",
                               rangeAt(context, declaration->getLocation()));
        }
        auto qualifier = qualifierFrom(targetHere, declaration->getLexicalDeclContext());
        if (!qualifier || qualifier->empty()) {
            return makeRefusal(codes::UnsupportedReference, "The target can't be named from the definition.",
                               rangeAt(context, declaration->getLocation()));
        }
        if (!plan_.replace(name->file, begin->offset, name->offset - begin->offset, spell(*qualifier) + "::")) {
            return makeRefusal(codes::UnsupportedReference, "Two edits overlap.",
                               rangeAt(context, declaration->getLocation()));
        }
        plan_.expectQualifier(name->file, begin->offset, name->offset - begin->offset, 0, *qualifier);
        std::vector<InnerEdit> none;
        if (auto refusal = qualifyNamesFromSource(declaration, /*moves=*/false, none)) {
            return refusal;
        }
    }

    // Insert the moved text in the target.
    const auto *targetHere = targetIn(movingContext);
    SourceLocation closingBrace;
    if (const auto *record = dyn_cast<CXXRecordDecl>(targetHere)) {
        closingBrace = record->getBraceRange().getEnd();
    } else {
        for (const auto *decl : cast<NamespaceDecl>(targetHere)->redecls()) {
            auto braceFile = fileOffsetOfLocation(movingContext, decl->getRBraceLoc());
            if (braceFile && braceFile->file == range->file) {
                closingBrace = decl->getRBraceLoc();
            }
        }
    }
    auto brace = fileOffsetOfLocation(movingContext, closingBrace);
    if (!brace) {
        return makeRefusal(codes::UnsupportedTarget,
                           "The tool moves a namespace-scope declaration only into a block of the target namespace "
                           "in the same file.",
                           rangeAt(movingContext, moving->getLocation()),
                           "Open a `namespace` block for the target in that file first.");
    }
    const auto &destinationText = plan_.text(brace->file);
    std::string prefix;
    unsigned lineStart = brace->offset;
    while (lineStart > 0 && (destinationText[lineStart - 1] == ' ' || destinationText[lineStart - 1] == '\t')) {
        --lineStart;
    }
    bool braceStartsLine = lineStart == 0 || destinationText[lineStart - 1] == '\n';
    unsigned insertAt = braceStartsLine ? lineStart : brace->offset;
    if (!braceStartsLine) {
        prefix += "\n";
    }
    const auto &movedOriginal = plan_.text(range->file);
    std::string moved = movedOriginal.substr(range->begin, range->end - range->begin);
    std::string indentation;
    for (char character : moved) {
        if (character != ' ' && character != '\t') {
            break;
        }
        indentation += character;
    }
    if (memberOfClass_) {
        std::string labelIndentation = indentation.size() >= 2 ? indentation.substr(2) : "";
        prefix += labelIndentation + accessSpelling(moving->getAccess()) + ":\n";
    }
    std::vector<Replacement> inner;
    for (const auto &edit : innerEdits) {
        inner.push_back(edit.replacement);
    }
    EditMap innerMap;
    for (const auto &replacement : inner) {
        if (!innerMap.add("", replacement)) {
            return makeRefusal(codes::UnsupportedReference, "Two edits in the moved declaration overlap.");
        }
    }
    auto movedText = innerMap.apply("", moved);
    std::string suffix = movedText.ends_with("\n") ? "" : "\n";
    if (!braceStartsLine) {
        suffix += indentation.size() >= 4 ? indentation.substr(4) : "";
    }
    if (!plan_.replace(range->file, range->begin, range->end - range->begin, "") ||
        !plan_.insert(brace->file, insertAt, prefix + movedText + suffix)) {
        return makeRefusal(codes::UnsupportedReference, "The moved declaration overlaps another edit.");
    }
    plan_.relocate(range->file, range->begin, range->end - range->begin, brace->file, insertAt, 0, prefix.size(),
                   inner);
    for (const auto &edit : innerEdits) {
        unsigned start = EditPlan::offsetInCopy(inner, edit.replacement.offset);
        if (edit.replacement.length == 0) {
            start -= edit.replacement.newText.size();
        }
        plan_.expectQualifier(brace->file, insertAt, 0, prefix.size() + start + edit.qualifierPosition, edit.qualifier);
    }
    return std::nullopt;
}

std::optional<RefusalReason> MoveStaticMemberPlanner::verify() {
    return verifyEditPlan(workspace_, loaded_, plan_, {}, "moving the member");
}

RefactoringResult moveStaticMember(const Workspace &workspace, const MoveStaticMemberRequest &request) {
    auto refuse = [&](RefusalReason reason) { return RefactoringResult::refused(workspace.root(), std::move(reason)); };
    MoveStaticMemberPlanner planner(workspace, request);
    if (auto refusal = planner.parseAndCollect()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.findMemberAndTarget()) {
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
