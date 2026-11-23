#include "Rename.h"

#include "NameChecks.h"
#include "RawTokens.h"
#include "RefusalCodes.h"
#include "SourceText.h"
#include "Verification.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/CharInfo.h"

namespace riktig {

using namespace clang;
namespace codes = refusal_codes;

namespace {

// The name a rename edits: a constructor, destructor or deduction guide is spelled with its class's name.
std::string spelledNameOf(const NamedDecl *declaration) {
    if (const auto *constructor = dyn_cast<CXXConstructorDecl>(declaration)) {
        return constructor->getParent()->getName().str();
    }
    if (const auto *destructor = dyn_cast<CXXDestructorDecl>(declaration)) {
        return destructor->getParent()->getName().str();
    }
    if (const auto *guide = dyn_cast<CXXDeductionGuideDecl>(declaration)) {
        return guide->getDeducedTemplate()->getName().str();
    }
    if (const auto *functionTemplate = dyn_cast<FunctionTemplateDecl>(declaration)) {
        return spelledNameOf(functionTemplate->getTemplatedDecl());
    }
    if (declaration->getDeclName().isIdentifier()) {
        return declaration->getName().str();
    }
    return {};
}

const NamedDecl *entityRoot(const NamedDecl *declaration) {
    if (const auto *shadow = dyn_cast<UsingShadowDecl>(declaration)) {
        return entityRoot(shadow->getTargetDecl());
    }
    if (isa<CXXConstructorDecl, CXXDestructorDecl>(declaration)) {
        return entityRoot(cast<CXXMethodDecl>(declaration)->getParent());
    }
    if (const auto *guide = dyn_cast<CXXDeductionGuideDecl>(declaration)) {
        return entityRoot(guide->getDeducedTemplate());
    }
    if (const auto *specialization = dyn_cast<ClassTemplateSpecializationDecl>(declaration)) {
        return entityRoot(specialization->getSpecializedTemplate());
    }
    if (const auto *record = dyn_cast<CXXRecordDecl>(declaration)) {
        if (record->isInjectedClassName()) {
            return entityRoot(cast<CXXRecordDecl>(record->getDeclContext()));
        }
        if (auto *pattern = record->getInstantiatedFromMemberClass()) {
            return entityRoot(pattern);
        }
        if (auto *described = record->getDescribedClassTemplate()) {
            return described;
        }
    }
    if (const auto *function = dyn_cast<FunctionDecl>(declaration)) {
        if (auto *primary = function->getPrimaryTemplate()) {
            return entityRoot(primary);
        }
        if (auto *pattern = function->getInstantiatedFromMemberFunction()) {
            return entityRoot(pattern);
        }
        if (auto *described = function->getDescribedFunctionTemplate()) {
            return described;
        }
    }
    if (const auto *functionTemplate = dyn_cast<FunctionTemplateDecl>(declaration)) {
        if (isa<CXXConstructorDecl, CXXDestructorDecl, CXXDeductionGuideDecl>(functionTemplate->getTemplatedDecl())) {
            return entityRoot(functionTemplate->getTemplatedDecl());
        }
        if (auto *pattern = functionTemplate->getInstantiatedFromMemberTemplate()) {
            return entityRoot(pattern);
        }
    }
    if (const auto *classTemplate = dyn_cast<ClassTemplateDecl>(declaration)) {
        if (auto *pattern = classTemplate->getInstantiatedFromMemberTemplate()) {
            return entityRoot(pattern);
        }
    }
    if (const auto *variable = dyn_cast<VarDecl>(declaration)) {
        if (auto *pattern = variable->getInstantiatedFromStaticDataMember()) {
            return entityRoot(pattern);
        }
        if (auto *described = variable->getDescribedVarTemplate()) {
            return described;
        }
    }
    if (const auto *specialization = dyn_cast<VarTemplateSpecializationDecl>(declaration)) {
        return entityRoot(specialization->getSpecializedTemplate());
    }
    if (const auto *alias = dyn_cast<TypeAliasDecl>(declaration)) {
        if (auto *described = alias->getDescribedAliasTemplate()) {
            return described;
        }
    }
    if (const auto *enumeration = dyn_cast<EnumDecl>(declaration)) {
        if (auto *pattern = enumeration->getInstantiatedFromMemberEnum()) {
            return pattern;
        }
    }
    return declaration;
}

void addRelatedDeclarations(const NamedDecl *declaration, std::vector<const NamedDecl *> &pending) {
    for (const auto *redeclaration : declaration->redecls()) {
        pending.push_back(cast<NamedDecl>(redeclaration));
    }
    auto addRecordMembersNamedLikeIt = [&](const CXXRecordDecl *record) {
        if (!record) {
            return;
        }
        for (const auto *member : record->decls()) {
            if (const auto *named = dyn_cast<NamedDecl>(member)) {
                if (isa<CXXConstructorDecl, CXXDestructorDecl>(named)) {
                    pending.push_back(named);
                } else if (const auto *nested = dyn_cast<CXXRecordDecl>(named);
                           nested && nested->isInjectedClassName()) {
                    pending.push_back(named);
                } else if (const auto *constructorTemplate = dyn_cast<FunctionTemplateDecl>(named);
                           constructorTemplate && isa<CXXConstructorDecl>(constructorTemplate->getTemplatedDecl())) {
                    pending.push_back(constructorTemplate->getTemplatedDecl());
                }
            }
        }
    };
    auto addDeductionGuides = [&](const TemplateDecl *deducedTemplate) {
        for (const auto *sibling : deducedTemplate->getDeclContext()->decls()) {
            const auto *guide = dyn_cast<CXXDeductionGuideDecl>(sibling);
            if (const auto *guideTemplate = dyn_cast<FunctionTemplateDecl>(sibling)) {
                guide = dyn_cast<CXXDeductionGuideDecl>(guideTemplate->getTemplatedDecl());
            }
            if (guide && guide->getDeducedTemplate() == deducedTemplate) {
                pending.push_back(guide);
            }
        }
    };
    if (const auto *classTemplate = dyn_cast<ClassTemplateDecl>(declaration)) {
        pending.push_back(classTemplate->getTemplatedDecl());
        for (const auto *specialization : classTemplate->specializations()) {
            pending.push_back(specialization);
            addRecordMembersNamedLikeIt(specialization);
        }
        llvm::SmallVector<ClassTemplatePartialSpecializationDecl *> partials;
        const_cast<ClassTemplateDecl *>(classTemplate)->getPartialSpecializations(partials);
        for (const auto *partial : partials) {
            pending.push_back(partial);
            addRecordMembersNamedLikeIt(partial);
        }
        addDeductionGuides(classTemplate);
    }
    if (const auto *record = dyn_cast<CXXRecordDecl>(declaration)) {
        addRecordMembersNamedLikeIt(record);
        if (auto *described = record->getDescribedClassTemplate()) {
            pending.push_back(described);
        }
    }
    if (const auto *functionTemplate = dyn_cast<FunctionTemplateDecl>(declaration)) {
        pending.push_back(functionTemplate->getTemplatedDecl());
        for (const auto *specialization : functionTemplate->specializations()) {
            pending.push_back(specialization);
        }
    }
    if (const auto *function = dyn_cast<FunctionDecl>(declaration)) {
        if (auto *described = function->getDescribedFunctionTemplate()) {
            pending.push_back(described);
        }
    }
    if (const auto *method = dyn_cast<CXXMethodDecl>(declaration)) {
        for (const auto *overridden : method->overridden_methods()) {
            pending.push_back(overridden);
        }
    }
    if (const auto *variableTemplate = dyn_cast<VarTemplateDecl>(declaration)) {
        pending.push_back(variableTemplate->getTemplatedDecl());
        for (const auto *specialization : variableTemplate->specializations()) {
            pending.push_back(specialization);
        }
    }
    if (const auto *variable = dyn_cast<VarDecl>(declaration)) {
        if (auto *described = variable->getDescribedVarTemplate()) {
            pending.push_back(described);
        }
    }
    if (const auto *aliasTemplate = dyn_cast<TypeAliasTemplateDecl>(declaration)) {
        pending.push_back(aliasTemplate->getTemplatedDecl());
    }
    if (const auto *alias = dyn_cast<TypeAliasDecl>(declaration)) {
        if (auto *described = alias->getDescribedAliasTemplate()) {
            pending.push_back(described);
        }
    }
}

// Declarations the compiler makes, such as an implicit deduction guide and its parameters, which are located at the
// name of the declaration they are made from.
bool isCompilerGenerated(const NamedDecl *declaration) {
    if (declaration->isImplicit()) {
        return true;
    }
    const auto *context = dyn_cast<Decl>(declaration->getDeclContext());
    return isa<ParmVarDecl>(declaration) && context && context->isImplicit();
}

} // namespace

RenamePlanner::RenamePlanner(const Workspace &workspace, RenameRequest request)
    : workspace_(workspace), request_(std::move(request)) {}

std::optional<RefusalReason> RenamePlanner::parseAndCollect() {
    parsed_ = parse(workspace_, {});
    if (!parsed_.parsedEveryTranslationUnit || !parsed_.errors.empty()) {
        std::string firstError = parsed_.errors.empty() ? "a translation unit could not be parsed" : parsed_.errors[0];
        return makeRefusal(codes::WorkspaceDoesNotCompile,
                           "The workspace must compile before it can be refactored; " + firstError + ".");
    }
    translationUnits_ = collectBindings(workspace_, parsed_);
    return std::nullopt;
}

std::optional<RefusalReason> RenamePlanner::findSymbolAtCursor() {
    auto file = workspace_.absolutePath(request_.file);
    auto text = workspace_.read(file);
    if (!text) {
        llvm::consumeError(text.takeError());
        return makeRefusal(codes::InvalidLocation, "Cannot read " + request_.file + ".");
    }
    auto cursor = offsetOfPosition(*text, request_.at);
    std::optional<unsigned> tokenStart;
    if (cursor) {
        tokenStart = identifierStartAround(*text, *cursor);
    }
    if (!tokenStart) {
        return makeRefusal(codes::NoSymbol, "There is no identifier at the cursor.",
                           FileRange{workspace_.displayPath(file), request_.at, request_.at});
    }
    FileOffset token{file, *tokenStart};
    auto tokenRange = rangeOf(workspace_, token);
    std::map<DeclIdentity, const NamedDecl *> roots;
    bool inMacro = false;
    for (const auto &unit : translationUnits_) {
        if (auto found = unit.namedAtToken.find(token); found != unit.namedAtToken.end()) {
            for (const auto *declaration : found->second) {
                if (isCompilerGenerated(declaration) && !isa<CXXRecordDecl>(declaration)) {
                    continue;
                }
                const auto *root = entityRoot(declaration);
                roots.emplace(identityOf(root), root);
            }
        }
        inMacro = inMacro || unit.namedAtTokenInMacro.count(token);
    }
    if (roots.empty()) {
        if (inMacro) {
            return makeRefusal(codes::MacroInvolved, "The identifier at the cursor is part of a macro.", tokenRange);
        }
        return makeRefusal(codes::NoSymbol, "There is no renameable symbol at the cursor.", tokenRange);
    }
    if (roots.size() > 1) {
        std::string kinds;
        for (const auto &[identity, root] : roots) {
            kinds += (kinds.empty() ? "" : ", ") + std::string(root->getDeclKindName());
        }
        return makeRefusal(codes::AmbiguousSymbol,
                           "The identifier at the cursor refers to more than one symbol (" + kinds +
                               "), such as an overload set.",
                           tokenRange, "Put the cursor on the symbol's declaration.");
    }
    symbol_ = roots.begin()->second;
    oldName_ = spelledNameOf(symbol_);
    if (oldName_.empty()) {
        return makeRefusal(codes::NotRenameable, "Only symbols named by an identifier can be renamed.", tokenRange);
    }
    return std::nullopt;
}

std::optional<RefusalReason> RenamePlanner::expandTargets() {
    std::set<const NamedDecl *> processed;
    std::vector<const NamedDecl *> pending{symbol_};
    std::vector<std::vector<const NamedDecl *>> candidatesPerUnit;
    for (const auto &unit : translationUnits_) {
        std::vector<const NamedDecl *> candidates;
        for (const auto *declaration : unit.declarations) {
            if (spelledNameOf(declaration) == oldName_) {
                candidates.push_back(declaration);
            }
        }
        candidatesPerUnit.push_back(std::move(candidates));
    }
    bool changed = true;
    while (changed) {
        changed = false;
        while (!pending.empty()) {
            const auto *declaration = pending.back();
            pending.pop_back();
            if (!processed.insert(declaration).second) {
                continue;
            }
            changed = true;
            targetDeclarations_.push_back(declaration);
            targets_.insert(identityOf(declaration));
            if (auto usr = usrOf(declaration); !usr.empty()) {
                targetUsrs_.insert(usr);
            }
            addRelatedDeclarations(declaration, pending);
        }
        for (const auto &candidates : candidatesPerUnit) {
            for (const auto *candidate : candidates) {
                if (processed.count(candidate)) {
                    continue;
                }
                bool sameEntity = targets_.count(identityOf(candidate)) || targetUsrs_.count(usrOf(candidate));
                bool overridesTarget = false;
                if (const auto *method = dyn_cast<CXXMethodDecl>(candidate)) {
                    for (const auto *overridden : method->overridden_methods()) {
                        overridesTarget = overridesTarget || targets_.count(identityOf(overridden));
                    }
                }
                if (sameEntity || overridesTarget) {
                    pending.push_back(candidate);
                }
            }
        }
    }

    for (const auto *declaration : targetDeclarations_) {
        auto identity = identityOf(declaration);
        auto location = rangeOf(workspace_, identity.location);
        if (spelledNameOf(declaration) != oldName_) {
            return makeRefusal(codes::NotRenameable,
                               "The symbol is related to `" + declaration->getNameAsString() +
                                   "`, which has a different name, so the tool cannot rename them together.",
                               location);
        }
        if (identity.location.file.empty() || !workspace_.contains(identity.location.file)) {
            return makeRefusal(codes::NotRenameable,
                               "`" + oldName_ + "` is declared outside the workspace (" +
                                   (identity.location.file.empty() ? "built in" : identity.location.file) +
                                   "), or overrides a member declared there.");
        }
        if (identity.tag.find('@') != std::string::npos) {
            return makeRefusal(codes::MacroInvolved, "`" + oldName_ + "` is declared by a macro.", location);
        }
        if (const auto *function = dyn_cast<FunctionDecl>(declaration)) {
            if (function->isMain()) {
                return makeRefusal(codes::NotRenameable, "`main` is the program's entry point.", location);
            }
        }
        if (isa<FunctionDecl, VarDecl>(declaration) && declaration->getDeclContext()->isExternCContext() &&
            !declaration->getParentFunctionOrMethod()) {
            return makeRefusal(codes::NotRenameable,
                               "`" + oldName_ +
                                   "` has C language linkage, so code outside the workspace may "
                                   "refer to it by name.",
                               location);
        }
    }
    return std::nullopt;
}

std::optional<RefusalReason> RenamePlanner::collectEdits(bool refuseUnsafeTokens) {
    auto bindsTarget = [&](const std::vector<const NamedDecl *> &declarations) {
        return llvm::any_of(declarations,
                            [&](const NamedDecl *declaration) { return targets_.count(identityOf(declaration)); });
    };
    std::map<std::string, std::string> texts;
    auto textOf = [&](const std::string &file) -> const std::string & {
        auto found = texts.find(file);
        if (found == texts.end()) {
            auto text = workspace_.read(file);
            std::string content;
            if (text) {
                content = std::move(*text);
            } else {
                llvm::consumeError(text.takeError());
            }
            found = texts.emplace(file, std::move(content)).first;
        }
        return found->second;
    };
    for (const auto &unit : translationUnits_) {
        for (const auto &[token, declarations] : unit.namedAtToken) {
            if (!bindsTarget(declarations)) {
                continue;
            }
            if (identifierAt(textOf(token.file), token.offset) != oldName_) {
                continue;
            }
            if (!edits_.add(token.file,
                            Replacement{token.offset, static_cast<unsigned>(oldName_.size()), request_.newName})) {
                return makeRefusal(codes::InternalVerificationFailed, "Two edits of the rename overlap.",
                                   rangeOf(workspace_, token));
            }
        }
        if (!refuseUnsafeTokens) {
            continue;
        }
        for (const auto &[token, declarations] : unit.namedAtTokenInMacro) {
            if (bindsTarget(declarations)) {
                return makeRefusal(codes::MacroInvolved,
                                   "A macro refers to `" + oldName_ + "`, and the tool does not rename inside macros.",
                                   rangeOf(workspace_, token));
            }
        }
        for (const auto &[key, identities] : unit.bindings) {
            if (workspace_.contains(key.location.file) || key.role == "decl") {
                continue;
            }
            bool bindsTargetIdentity =
                llvm::any_of(identities, [&](const DeclIdentity &identity) { return targets_.count(identity); });
            if (bindsTargetIdentity && identifierAt(textOf(key.location.file), key.location.offset) == oldName_) {
                return makeRefusal(codes::ReferenceOutsideWorkspace,
                                   "Code outside the workspace refers to `" + oldName_ + "` by name, at " +
                                       key.location.file + " (offset " + std::to_string(key.location.offset) +
                                       "), for example through argument-dependent lookup in a template.");
            }
        }
    }
    return std::nullopt;
}

RefactoringResult rename(const Workspace &workspace, const RenameRequest &request) {
    auto refuse = [&](RefusalReason reason) { return RefactoringResult::refused(workspace.root(), std::move(reason)); };
    if (!isValidIdentifier(request.newName)) {
        return refuse(*validateNewName("", request.newName));
    }
    RenamePlanner planner(workspace, request);
    if (auto refusal = planner.parseAndCollect()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.findSymbolAtCursor()) {
        return refuse(*refusal);
    }
    if (auto refusal = validateNewName(planner.oldName(), request.newName)) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.expandTargets()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.collectEdits(/*refuseUnsafeTokens=*/true)) {
        return refuse(*refusal);
    }
    auto &before = planner.translationUnits();
    for (const auto &unit : before) {
        for (const auto &name : {planner.oldName(), request.newName}) {
            if (unit.macroNames.count(name)) {
                return refuse(makeRefusal(codes::MacroInvolved, "`" + name + "` is also the name of a macro."));
            }
        }
    }
    std::set<std::string> names{planner.oldName(), request.newName};
    if (auto refusal = checkEveryMentionIsUnderstood(workspace, before, names, {})) {
        return refuse(*refusal);
    }

    const auto &edits = planner.edits();
    auto overlay = applyEdits(workspace, edits);
    auto parsedAfter = parse(workspace, overlay);
    if (!parsedAfter.parsedEveryTranslationUnit || !parsedAfter.errors.empty()) {
        std::string firstError =
            parsedAfter.errors.empty() ? "a translation unit could not be parsed" : parsedAfter.errors[0];
        return refuse(makeRefusal(codes::WouldIntroduceErrors,
                                  "After the rename, the code would not compile: " + firstError + "."));
    }
    auto after = collectBindings(workspace, parsedAfter);

    std::set<DeclIdentity> movedTargets;
    for (const auto &target : planner.targets()) {
        movedTargets.insert(moved(edits, target));
    }
    std::set<std::string> existingUsrsOfNewName;
    for (const auto &unit : before) {
        for (const auto *declaration : unit.declarations) {
            if (spelledNameOf(declaration) == request.newName) {
                existingUsrsOfNewName.insert(usrOf(declaration));
            }
        }
    }
    for (const auto &unit : after) {
        for (const auto *declaration : unit.declarations) {
            if (!movedTargets.count(identityOf(declaration))) {
                continue;
            }
            if (auto usr = usrOf(declaration); !usr.empty() && existingUsrsOfNewName.count(usr)) {
                auto location = edits.unmap(identityOf(declaration).location);
                return refuse(makeRefusal(codes::ConflictingDeclaration,
                                          "The renamed symbol would be the same entity as an existing `" +
                                              request.newName + "` declared elsewhere in the program.",
                                          rangeOf(workspace, location)));
            }
        }
    }

    if (auto difference = compareBindings(before, after, edits, {})) {
        auto location = edits.unmap(difference->location);
        return refuse(
            makeRefusal(codes::BindingWouldChange,
                        "After the rename, a reference would bind differently: " + difference->description + ".",
                        rangeOf(workspace, location)));
    }
    if (auto refusal = checkEveryMentionIsUnderstood(workspace, after, names, overlay)) {
        auto reason = *refusal;
        reason.code = codes::BindingWouldChange;
        return refuse(reason);
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, edits));
}

} // namespace riktig
