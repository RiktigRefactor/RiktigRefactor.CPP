#include "FunctionRefactoring.h"

#include "RawTokens.h"
#include "RefusalCodes.h"
#include "Results.h"
#include "SourceText.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

namespace riktig {

using namespace clang;
namespace codes = refusal_codes;

std::optional<RefusalReason> loadWorkspace(const Workspace &workspace, LoadedWorkspace &loaded) {
    loaded.parsed = parse(workspace, {});
    if (!loaded.parsed.parsedEveryTranslationUnit || !loaded.parsed.errors.empty()) {
        std::string firstError =
            loaded.parsed.errors.empty() ? "a translation unit could not be parsed" : loaded.parsed.errors[0];
        return makeRefusal(codes::WorkspaceDoesNotCompile,
                           "The workspace must compile before it can be refactored; " + firstError + ".");
    }
    loaded.units = collectBindings(workspace, loaded.parsed);
    return std::nullopt;
}

static const NamedDecl *declarationRoot(const NamedDecl *declaration) {
    if (const auto *shadow = dyn_cast<UsingShadowDecl>(declaration)) {
        return declarationRoot(shadow->getTargetDecl());
    }
    if (const auto *function = dyn_cast<FunctionDecl>(declaration)) {
        if (auto *described = function->getDescribedFunctionTemplate()) {
            return described;
        }
        if (auto *primary = function->getPrimaryTemplate()) {
            return primary;
        }
        if (auto *pattern = function->getInstantiatedFromMemberFunction()) {
            return declarationRoot(pattern);
        }
    }
    if (const auto *variable = dyn_cast<VarDecl>(declaration)) {
        if (auto *pattern = variable->getInstantiatedFromStaticDataMember()) {
            return declarationRoot(pattern);
        }
    }
    return cast<NamedDecl>(declaration->getCanonicalDecl());
}

std::optional<RefusalReason> findDeclarationAtCursor(const Workspace &workspace, const LoadedWorkspace &loaded,
                                                     const std::string &requestFile, Position at,
                                                     const NamedDecl *&found) {
    auto file = workspace.absolutePath(requestFile);
    auto text = workspace.read(file);
    if (!text) {
        llvm::consumeError(text.takeError());
        return makeRefusal(codes::InvalidLocation, "Cannot read " + requestFile + ".");
    }
    auto cursor = offsetOfPosition(*text, at);
    std::optional<unsigned> tokenStart;
    if (cursor) {
        tokenStart = identifierStartAround(*text, *cursor);
    }
    if (!tokenStart) {
        return makeRefusal(codes::NoSymbol, "There is no identifier at the cursor.",
                           FileRange{workspace.displayPath(file), at, at});
    }
    FileOffset token{file, *tokenStart};
    auto tokenRange = rangeOf(workspace, token);
    std::map<std::string, const NamedDecl *> roots;
    bool inMacro = false;
    for (const auto &unit : loaded.units) {
        if (auto named = unit.namedAtToken.find(token); named != unit.namedAtToken.end()) {
            for (const auto *declaration : named->second) {
                if (declaration->isImplicit() || isa<ParmVarDecl>(declaration)) {
                    continue;
                }
                const auto *root = declarationRoot(declaration);
                auto key = usrOf(root);
                roots.emplace(key.empty() ? identityOf(root).describe() : key, root);
            }
        }
        inMacro = inMacro || unit.namedAtTokenInMacro.count(token);
    }
    if (roots.empty()) {
        if (inMacro) {
            return makeRefusal(codes::MacroInvolved, "The identifier at the cursor is part of a macro.", tokenRange);
        }
        return makeRefusal(codes::NoSymbol, "There is no symbol at the cursor.", tokenRange);
    }
    if (roots.size() > 1) {
        return makeRefusal(codes::AmbiguousSymbol, "The identifier at the cursor refers to more than one symbol.",
                           tokenRange, "Put the cursor on the declaration.");
    }
    found = roots.begin()->second;
    return std::nullopt;
}

std::optional<FileOffset> fileOffsetOfLocation(const ASTContext &context, SourceLocation location) {
    if (location.isInvalid() || location.isMacroID()) {
        return std::nullopt;
    }
    return fileOffsetOf(context.getSourceManager(), location);
}

std::optional<unsigned> endOfTokenAt(const ASTContext &context, SourceLocation location) {
    if (location.isInvalid() || location.isMacroID()) {
        return std::nullopt;
    }
    auto end = Lexer::getLocForEndOfToken(location, 0, context.getSourceManager(), context.getLangOpts());
    auto offset = fileOffsetOf(context.getSourceManager(), end);
    if (!offset) {
        return std::nullopt;
    }
    return offset->offset;
}

std::optional<OffsetRange> fileRangeOf(const ASTContext &context, SourceRange range) {
    auto begin = fileOffsetOfLocation(context, range.getBegin());
    auto endFile = fileOffsetOfLocation(context, range.getEnd());
    auto end = endOfTokenAt(context, range.getEnd());
    if (!begin || !endFile || !end || begin->file != endFile->file || *end < begin->offset) {
        return std::nullopt;
    }
    return OffsetRange{begin->file, begin->offset, *end};
}

std::optional<OffsetRange> nextTokenAfter(const ASTContext &context, SourceLocation location) {
    if (location.isInvalid() || location.isMacroID()) {
        return std::nullopt;
    }
    auto token = Lexer::findNextToken(location, context.getSourceManager(), context.getLangOpts());
    if (!token || token->getLocation().isMacroID()) {
        return std::nullopt;
    }
    auto begin = fileOffsetOf(context.getSourceManager(), token->getLocation());
    if (!begin) {
        return std::nullopt;
    }
    return OffsetRange{begin->file, begin->offset, begin->offset + token->getLength()};
}

namespace {

bool isTemplateInstantiation(const Decl *declaration) {
    if (const auto *function = dyn_cast<FunctionDecl>(declaration)) {
        return function->isTemplateInstantiation();
    }
    if (const auto *record = dyn_cast<CXXRecordDecl>(declaration)) {
        return isTemplateInstantiation(record->getTemplateSpecializationKind());
    }
    if (const auto *variable = dyn_cast<VarDecl>(declaration)) {
        return isTemplateInstantiation(variable->getTemplateSpecializationKind());
    }
    return false;
}

class ReferenceFinder : public RecursiveASTVisitor<ReferenceFinder> {
    using Base = RecursiveASTVisitor<ReferenceFinder>;

  public:
    ReferenceFinder(const Workspace *workspace, TranslationUnitBindings &unit, const std::string &usr,
                    const std::string &name, std::vector<Reference> &out)
        : workspace_(workspace), unit_(unit), context_(unit.unit->getASTContext()), usr_(usr), name_(name), out_(out) {}

    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return true; }

    bool TraverseDecl(Decl *declaration) {
        if (!declaration) {
            return true;
        }
        bool instantiation = isTemplateInstantiation(declaration);
        instantiationDepth_ += instantiation;
        stack_.push_back(declaration);
        bool result = Base::TraverseDecl(declaration);
        stack_.pop_back();
        instantiationDepth_ -= instantiation;
        return result;
    }

    bool VisitDeclRefExpr(DeclRefExpr *expression) {
        consider(expression, expression->getDecl(), expression->getLocation());
        return true;
    }

    bool VisitMemberExpr(MemberExpr *expression) {
        consider(expression, expression->getMemberDecl(), expression->getMemberLoc());
        return true;
    }

  private:
    bool isTarget(const ValueDecl *declaration) {
        if (!isa<FunctionDecl, VarDecl>(declaration) || !declaration->getDeclName().isIdentifier() ||
            declaration->getName() != name_) {
            return false;
        }
        if (const auto *function = dyn_cast<FunctionDecl>(declaration)) {
            if (auto *pattern = function->getInstantiatedFromMemberFunction()) {
                declaration = pattern;
            }
        }
        if (const auto *variable = dyn_cast<VarDecl>(declaration)) {
            if (auto *pattern = variable->getInstantiatedFromStaticDataMember()) {
                declaration = pattern;
            }
        }
        return usrOf(declaration) == usr_;
    }

    void consider(const Expr *expression, const ValueDecl *declaration, SourceLocation nameLocation) {
        if (!isTarget(declaration)) {
            return;
        }
        Reference reference;
        reference.unit = &unit_;
        reference.context = &context_;
        reference.expression = expression;
        reference.inTemplateInstantiation = instantiationDepth_ > 0;
        reference.inMacro = nameLocation.isMacroID();
        auto spelled = context_.getSourceManager().getSpellingLoc(nameLocation);
        auto offset = fileOffsetOf(context_.getSourceManager(), context_.getSourceManager().getExpansionLoc(spelled));
        reference.inWorkspace = offset && workspace_->contains(offset->file);
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            if (!reference.semanticContext) {
                if (const auto *context = dyn_cast<DeclContext>(*it)) {
                    reference.semanticContext = context;
                }
            }
            if (const auto *function = dyn_cast<FunctionDecl>(*it); function && !reference.enclosingFunction) {
                reference.enclosingFunction = function;
            }
        }
        const Stmt *child = expression;
        auto parents = context_.getParents(*child);
        while (parents.size() == 1) {
            const auto *parent = parents[0].get<Stmt>();
            if (const auto *cast = dyn_cast_or_null<ImplicitCastExpr>(parent);
                cast && cast->getCastKind() == CK_FunctionToPointerDecay) {
                child = parent;
                parents = context_.getParents(*child);
                continue;
            }
            if (const auto *call = dyn_cast_or_null<CallExpr>(parent); call && call->getCallee() == child) {
                reference.call = call;
            }
            break;
        }
        out_.push_back(reference);
    }

    const Workspace *workspace_;
    TranslationUnitBindings &unit_;
    ASTContext &context_;
    const std::string &usr_;
    const std::string &name_;
    std::vector<Reference> &out_;
    std::vector<const Decl *> stack_;
    int instantiationDepth_ = 0;
};

} // namespace

std::vector<Reference> findReferences(const Workspace &workspace, LoadedWorkspace &loaded, const ValueDecl *target) {
    std::vector<Reference> references;
    auto usr = usrOf(target);
    auto name = target->getName().str();
    for (auto &unit : loaded.units) {
        ReferenceFinder finder(&workspace, unit, usr, name, references);
        finder.TraverseDecl(unit.unit->getASTContext().getTranslationUnitDecl());
    }
    return references;
}

std::vector<const ValueDecl *> declarationsOf(const LoadedWorkspace &loaded, const ValueDecl *target) {
    auto usr = usrOf(target);
    std::map<FileOffset, const ValueDecl *> byLocation;
    for (const auto &unit : loaded.units) {
        for (const auto *declaration : unit.declarations) {
            const auto *candidate = dyn_cast<ValueDecl>(declaration);
            if (!candidate || !isa<FunctionDecl, VarDecl>(candidate) || !candidate->getDeclName().isIdentifier() ||
                candidate->getName() != target->getName()) {
                continue;
            }
            if (const auto *function = dyn_cast<FunctionDecl>(candidate);
                function && function->isTemplateInstantiation()) {
                continue;
            }
            if (const auto *variable = dyn_cast<VarDecl>(candidate);
                variable && isTemplateInstantiation(variable->getTemplateSpecializationKind())) {
                continue;
            }
            if (usrOf(candidate) != usr) {
                continue;
            }
            auto location = fileOffsetOf(candidate->getASTContext().getSourceManager(), candidate->getLocation());
            if (location) {
                byLocation.emplace(*location, candidate);
            }
        }
    }
    std::vector<const ValueDecl *> result;
    for (const auto &[location, declaration] : byLocation) {
        result.push_back(declaration);
    }
    return result;
}

std::vector<const Expr *> writtenArguments(const CallExpr *call) {
    std::vector<const Expr *> arguments;
    for (const auto *argument : call->arguments()) {
        if (!isa<CXXDefaultArgExpr>(argument)) {
            arguments.push_back(argument);
        }
    }
    return arguments;
}

static std::vector<const DeclContext *> namedChain(const DeclContext *context) {
    std::vector<const DeclContext *> chain;
    for (; context && !context->isTranslationUnit(); context = context->getParent()) {
        chain.insert(chain.begin(), context);
    }
    return chain;
}

std::optional<std::vector<QualifierComponent>> qualifierFrom(const DeclContext *target, const DeclContext *from) {
    // The contexts may be in different translation units, so they are compared by name.
    auto targetChain = namedChain(target);
    std::vector<const DeclContext *> fromNamespaces;
    for (const auto *context : namedChain(from)) {
        if (!isa<NamespaceDecl>(context) || cast<NamespaceDecl>(context)->isAnonymousNamespace()) {
            break;
        }
        fromNamespaces.push_back(context);
    }
    size_t common = 0;
    while (common < targetChain.size() && common < fromNamespaces.size() && isa<NamespaceDecl>(targetChain[common]) &&
           cast<NamedDecl>(targetChain[common])->getQualifiedNameAsString() ==
               cast<NamedDecl>(fromNamespaces[common])->getQualifiedNameAsString()) {
        ++common;
    }
    std::vector<QualifierComponent> qualifier;
    for (size_t index = common; index < targetChain.size(); ++index) {
        const auto *context = targetChain[index];
        if (isa<LinkageSpecDecl>(context)) {
            continue;
        }
        if (const auto *ns = dyn_cast<NamespaceDecl>(context); ns && ns->isAnonymousNamespace()) {
            continue;
        }
        const auto *named = dyn_cast<NamedDecl>(context);
        if (!named || !isa<NamespaceDecl, CXXRecordDecl>(named) || !named->getDeclName().isIdentifier() ||
            named->getName().empty()) {
            return std::nullopt;
        }
        qualifier.push_back(QualifierComponent{named, named->getName().str()});
    }
    return qualifier;
}

std::string spell(const std::vector<QualifierComponent> &qualifier) {
    std::string result;
    for (const auto &component : qualifier) {
        result += (result.empty() ? "" : "::") + component.name;
    }
    return result;
}

void EditPlan::expectQualifier(const std::string &file, unsigned replacementOffset, unsigned replacementLength,
                               unsigned positionInNewText, const std::vector<QualifierComponent> &qualifier) {
    unsigned position = positionInNewText;
    for (const auto &component : qualifier) {
        expectBinding(file, replacementOffset, replacementLength, position,
                      isa<NamespaceDecl>(component.declaration) ? "ref" : "type", identityOf(component.declaration));
        position += component.name.size() + 2;
    }
}

const std::string &EditPlan::text(const std::string &file) {
    auto found = texts_.find(file);
    if (found == texts_.end()) {
        auto content = workspace_.read(file);
        std::string text;
        if (content) {
            text = std::move(*content);
        } else {
            llvm::consumeError(content.takeError());
        }
        found = texts_.emplace(file, std::move(text)).first;
    }
    return found->second;
}

bool EditPlan::replace(const std::string &file, unsigned offset, unsigned length, std::string newText) {
    return edits_.add(file, Replacement{offset, length, std::move(newText)});
}

void EditPlan::relocate(const std::string &file, unsigned from, unsigned length, const std::string &destinationFile,
                        unsigned replacementOffset, unsigned replacementLength, unsigned positionInNewText,
                        std::vector<Replacement> innerEdits) {
    for (const auto &existing : relocations_) {
        if (existing.file == file && existing.from == from && existing.length == length) {
            return;
        }
    }
    std::sort(innerEdits.begin(), innerEdits.end(), [](const Replacement &left, const Replacement &right) {
        return std::pair(left.offset, left.length) < std::pair(right.offset, right.length);
    });
    relocations_.push_back(Relocation{file, from, length, destinationFile, replacementOffset, replacementLength,
                                      positionInNewText, std::move(innerEdits)});
}

unsigned EditPlan::offsetInCopy(const std::vector<Replacement> &innerEdits, unsigned relativeOffset) {
    EditMap map;
    for (const auto &edit : innerEdits) {
        map.add("", edit);
    }
    return map.mapOffset("", relativeOffset);
}

void EditPlan::expectBinding(const std::string &file, unsigned replacementOffset, unsigned replacementLength,
                             unsigned positionInNewText, std::string role, DeclIdentity identity,
                             bool identityIsAfter) {
    for (const auto &existing : expectations_) {
        if (existing.file == file && existing.replacementOffset == replacementOffset &&
            existing.replacementLength == replacementLength && existing.positionInNewText == positionInNewText &&
            existing.role == role) {
            return;
        }
    }
    expectations_.push_back(Expectation{file, replacementOffset, replacementLength, positionInNewText, std::move(role),
                                        std::move(identity), identityIsAfter});
}

unsigned EditPlan::afterOffset(const std::string &file, unsigned replacementOffset, unsigned replacementLength,
                               unsigned positionInNewText) const {
    unsigned start = edits_.mapOffset(file, replacementOffset);
    if (replacementLength == 0) {
        auto found = edits_.replacements().find(file);
        if (found != edits_.replacements().end()) {
            for (const auto &replacement : found->second) {
                if (replacement.offset == replacementOffset && replacement.length == 0) {
                    start -= replacement.newText.size();
                }
            }
        }
    }
    return start + positionInNewText;
}

std::optional<FileOffset> EditPlan::relocated(const FileOffset &before) const {
    for (const auto &relocation : relocations_) {
        if (relocation.file == before.file && relocation.from <= before.offset &&
            before.offset < relocation.from + relocation.length) {
            return FileOffset{relocation.destinationFile,
                              afterOffset(relocation.destinationFile, relocation.replacementOffset,
                                          relocation.replacementLength, relocation.positionInNewText) +
                                  offsetInCopy(relocation.innerEdits, before.offset - relocation.from)};
        }
    }
    return std::nullopt;
}

bool EditPlan::isInInsertedText(const FileOffset &after) const {
    auto found = edits_.replacements().find(after.file);
    if (found == edits_.replacements().end()) {
        return false;
    }
    for (const auto &replacement : found->second) {
        unsigned start = afterOffset(after.file, replacement.offset, replacement.length, 0);
        if (after.offset < start || after.offset >= start + replacement.newText.size()) {
            continue;
        }
        for (const auto &relocation : relocations_) {
            if (relocation.destinationFile != after.file || relocation.replacementOffset != replacement.offset ||
                relocation.replacementLength != replacement.length) {
                continue;
            }
            unsigned copyStart = start + relocation.positionInNewText;
            unsigned copyLength = offsetInCopy(relocation.innerEdits, relocation.length);
            if (after.offset < copyStart || after.offset >= copyStart + copyLength) {
                continue;
            }
            unsigned inCopy = after.offset - copyStart;
            // Text that an inner edit inserted is new; the rest is copied.
            long shift = 0;
            for (const auto &inner : relocation.innerEdits) {
                long newStart = static_cast<long>(inner.offset) + shift;
                if (newStart <= inCopy && inCopy < newStart + static_cast<long>(inner.newText.size())) {
                    return true;
                }
                shift += static_cast<long>(inner.newText.size()) - static_cast<long>(inner.length);
            }
            return false;
        }
        return true;
    }
    return false;
}

BindingExpectations EditPlan::expectations() const {
    BindingExpectations expectations;
    expectations.relocate = [this](const FileOffset &before) { return relocated(before); };
    // Text that a replacement deletes, and that is not copied elsewhere, loses its bindings.
    expectations.isDropped = [this](const BindingKey &key, const DeclIdentity &) {
        if (relocated(key.location)) {
            return false;
        }
        auto found = edits_.replacements().find(key.location.file);
        if (found == edits_.replacements().end()) {
            return false;
        }
        for (const auto &replacement : found->second) {
            if (replacement.offset <= key.location.offset &&
                key.location.offset < replacement.offset + replacement.length) {
                return true;
            }
        }
        return false;
    };
    expectations.isExpectedNew = [this](const BindingKey &key, const std::set<DeclIdentity> &identities) {
        if (!isInInsertedText(key.location)) {
            return false;
        }
        for (const auto &expectation : expectations_) {
            if (expectation.file != key.location.file || expectation.role != key.role ||
                afterOffset(expectation.file, expectation.replacementOffset, expectation.replacementLength,
                            expectation.positionInNewText) != key.location.offset) {
                continue;
            }
            auto identity = expectation.identity;
            if (!expectation.identityIsAfter && !identity.location.file.empty()) {
                identity.location = relocated(identity.location).value_or(edits_.map(identity.location));
            }
            return identities == std::set<DeclIdentity>{identity};
        }
        return false;
    };
    return expectations;
}

std::optional<std::string> EditPlan::missingExpectedBinding(const std::vector<TranslationUnitBindings> &after) const {
    for (const auto &expectation : expectations_) {
        BindingKey key{
            FileOffset{expectation.file, afterOffset(expectation.file, expectation.replacementOffset,
                                                     expectation.replacementLength, expectation.positionInNewText)},
            expectation.role};
        bool found = false;
        for (const auto &unit : after) {
            found = found || unit.bindings.count(key);
        }
        if (!found) {
            return "the inserted text at " + key.location.file + ":" + std::to_string(key.location.offset) +
                   " has no " + expectation.role + " binding";
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> verifyEditPlan(const Workspace &workspace, const LoadedWorkspace &before,
                                            const EditPlan &plan, const BindingExpectations &intended,
                                            const std::string &refactoringName) {
    const auto &edits = plan.edits();
    auto overlay = applyEdits(workspace, edits);
    auto parsedAfter = parse(workspace, overlay);
    if (!parsedAfter.parsedEveryTranslationUnit || !parsedAfter.errors.empty()) {
        std::string firstError =
            parsedAfter.errors.empty() ? "a translation unit could not be parsed" : parsedAfter.errors[0];
        return makeRefusal(codes::WouldIntroduceErrors,
                           "After " + refactoringName + ", the code would not compile: " + firstError + ".");
    }
    auto after = collectBindings(workspace, parsedAfter);
    auto expectations = plan.expectations();
    expectations.substitute = intended.substitute;
    expectations.isNew = intended.isNew;
    if (intended.isDropped) {
        expectations.isDropped = [planDropped = expectations.isDropped, intended](const BindingKey &key,
                                                                                  const DeclIdentity &identity) {
            return planDropped(key, identity) || intended.isDropped(key, identity);
        };
    }
    if (auto difference = compareBindings(before.units, after, edits, expectations)) {
        auto location = edits.unmap(difference->location);
        return makeRefusal(codes::BindingWouldChange,
                           "After " + refactoringName +
                               ", a reference would bind differently: " + difference->description + ".",
                           rangeOf(workspace, location));
    }
    if (auto missing = plan.missingExpectedBinding(after)) {
        return makeRefusal(codes::InternalVerificationFailed,
                           "The tool's check of its own result failed, which is a bug in the tool; please report "
                           "it. " +
                               *missing + ".");
    }
    return std::nullopt;
}

std::string textOf(EditPlan &plan, const OffsetRange &range) {
    return plan.text(range.file).substr(range.begin, range.end - range.begin);
}

} // namespace riktig
