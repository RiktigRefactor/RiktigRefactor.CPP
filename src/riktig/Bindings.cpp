#include "Bindings.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/Path.h"

namespace riktig {

using namespace clang;

std::string DeclIdentity::describe() const {
    if (location.file.empty()) {
        return tag;
    }
    return location.file + ":" + std::to_string(location.offset) + ":" + tag;
}

static std::string absolutePathOfFile(const SourceManager &sourceManager, FileID file) {
    auto entry = sourceManager.getFileEntryRefForID(file);
    if (!entry) {
        return {};
    }
    llvm::SmallString<256> path(entry->getName());
    sourceManager.getFileManager().makeAbsolutePath(path);
    llvm::sys::path::remove_dots(path, true);
    return std::string(path);
}

// A destructor's name location is its `~`; the identifier after it is the token a rename edits.
static unsigned skipTilde(const SourceManager &sourceManager, FileID file, unsigned offset) {
    auto buffer = sourceManager.getBufferData(file);
    if (offset >= buffer.size() || buffer[offset] != '~') {
        return offset;
    }
    ++offset;
    while (offset < buffer.size() &&
           (buffer[offset] == ' ' || buffer[offset] == '\t' || buffer[offset] == '\n' || buffer[offset] == '\r')) {
        ++offset;
    }
    return offset;
}

std::optional<FileOffset> fileOffsetOf(const SourceManager &sourceManager, SourceLocation location) {
    if (location.isInvalid() || !location.isFileID()) {
        return std::nullopt;
    }
    auto [file, offset] = sourceManager.getDecomposedLoc(location);
    auto path = absolutePathOfFile(sourceManager, file);
    if (path.empty()) {
        return std::nullopt;
    }
    return FileOffset{path, skipTilde(sourceManager, file, offset)};
}

struct NormalizedLocation {
    FileOffset location;
    // Empty for a token written directly in a file; "@macro" for one written in a macro definition or argument;
    // "@paste" for one made by token pasting, located at its expansion.
    std::string macroSuffix;
};

static std::optional<NormalizedLocation> normalize(const SourceManager &sourceManager, SourceLocation location) {
    if (location.isInvalid()) {
        return std::nullopt;
    }
    if (location.isFileID()) {
        auto fileOffset = fileOffsetOf(sourceManager, location);
        if (!fileOffset) {
            return std::nullopt;
        }
        return NormalizedLocation{*fileOffset, ""};
    }
    auto spelling = sourceManager.getSpellingLoc(location);
    if (!sourceManager.isWrittenInScratchSpace(spelling)) {
        if (auto fileOffset = fileOffsetOf(sourceManager, spelling)) {
            return NormalizedLocation{*fileOffset, "@macro"};
        }
    }
    if (auto fileOffset = fileOffsetOf(sourceManager, sourceManager.getExpansionLoc(location))) {
        return NormalizedLocation{*fileOffset, "@paste"};
    }
    return std::nullopt;
}

static std::string implicitRole(const NamedDecl *declaration) {
    if (const auto *record = dyn_cast<CXXRecordDecl>(declaration); record && record->isInjectedClassName()) {
        return "injected";
    }
    if (const auto *constructor = dyn_cast<CXXConstructorDecl>(declaration)) {
        if (constructor->isDefaultConstructor()) {
            return "default-constructor";
        }
        if (constructor->isCopyConstructor()) {
            return "copy-constructor";
        }
        if (constructor->isMoveConstructor()) {
            return "move-constructor";
        }
        return "constructor/" + std::to_string(constructor->getNumParams());
    }
    if (isa<CXXDestructorDecl>(declaration)) {
        return "destructor";
    }
    if (const auto *method = dyn_cast<CXXMethodDecl>(declaration)) {
        if (method->isCopyAssignmentOperator()) {
            return "copy-assignment";
        }
        if (method->isMoveAssignmentOperator()) {
            return "move-assignment";
        }
    }
    if (const auto *guide = dyn_cast<CXXDeductionGuideDecl>(declaration)) {
        unsigned index = 0;
        for (const auto *sibling : guide->getDeclContext()->decls()) {
            if (sibling == guide) {
                break;
            }
            if (isa<CXXDeductionGuideDecl>(sibling) && sibling->isImplicit()) {
                ++index;
            }
        }
        return "guide/" + std::to_string(index);
    }
    if (declaration->getDeclName().isIdentifier()) {
        return declaration->getName().str();
    }
    return declaration->getDeclName().getAsString();
}

DeclIdentity identityOf(const NamedDecl *declaration) {
    const auto *canonical = cast<NamedDecl>(declaration->getCanonicalDecl());
    const auto &sourceManager = canonical->getASTContext().getSourceManager();
    DeclIdentity identity;
    identity.tag = canonical->getDeclKindName();
    if (canonical->isImplicit()) {
        identity.tag += ":implicit:" + implicitRole(canonical);
    }
    if (auto normalized = normalize(sourceManager, canonical->getLocation())) {
        identity.location = normalized->location;
        identity.tag += normalized->macroSuffix;
    } else {
        identity.tag += ":" + canonical->getDeclName().getAsString();
    }
    return identity;
}

std::string usrOf(const Decl *declaration) {
    llvm::SmallString<128> usr;
    if (index::generateUSRForDecl(declaration, usr)) {
        return {};
    }
    return std::string(usr);
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

class BindingVisitor : public RecursiveASTVisitor<BindingVisitor> {
    using Base = RecursiveASTVisitor<BindingVisitor>;

  public:
    BindingVisitor(const Workspace &workspace, ASTContext &context, TranslationUnitBindings &out)
        : workspace_(workspace), sourceManager_(context.getSourceManager()), out_(out) {}

    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return true; }

    bool TraverseDecl(Decl *declaration) {
        bool instantiation = declaration && isTemplateInstantiation(declaration);
        instantiationDepth_ += instantiation;
        bool result = Base::TraverseDecl(declaration);
        instantiationDepth_ -= instantiation;
        return result;
    }

    bool VisitNamedDecl(NamedDecl *declaration) {
        out_.declarations.push_back(declaration);
        auto location = declaration->getLocation();
        record(location, "decl", declaration);
        if (auto *method = dyn_cast<CXXMethodDecl>(declaration)) {
            for (const auto *overridden : method->overridden_methods()) {
                record(location, "overrides", overridden);
            }
        }
        if (auto *function = dyn_cast<FunctionDecl>(declaration);
            function && function->getTemplateSpecializationKind() == TSK_ExplicitSpecialization) {
            record(location, "specializes", function->getPrimaryTemplate());
        }
        if (auto *specialization = dyn_cast<ClassTemplateSpecializationDecl>(declaration);
            specialization && specialization->getSpecializationKind() != TSK_ImplicitInstantiation) {
            record(location, "specializes", specialization->getSpecializedTemplate());
        }
        if (auto *usingDeclaration = dyn_cast<UsingDecl>(declaration)) {
            for (const auto *shadow : usingDeclaration->shadows()) {
                record(location, "using", shadow->getTargetDecl());
            }
        }
        if (auto *alias = dyn_cast<NamespaceAliasDecl>(declaration)) {
            record(alias->getTargetNameLoc(), "ref", alias->getAliasedNamespace());
        }
        return true;
    }

    bool VisitUsingDirectiveDecl(UsingDirectiveDecl *directive) {
        record(directive->getIdentLocation(), "ref", directive->getNominatedNamespaceAsWritten());
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr *expression) {
        record(expression->getLocation(), "ref", expression->getDecl());
        if (expression->getFoundDecl() != expression->getDecl()) {
            record(expression->getLocation(), "found", expression->getFoundDecl());
        }
        return true;
    }

    bool VisitMemberExpr(MemberExpr *expression) {
        record(expression->getMemberLoc(), "ref", expression->getMemberDecl());
        auto *found = expression->getFoundDecl().getDecl();
        if (found && found != expression->getMemberDecl()) {
            record(expression->getMemberLoc(), "found", found);
        }
        return true;
    }

    bool VisitCXXConstructExpr(CXXConstructExpr *expression) {
        record(expression->getLocation(), "construct", expression->getConstructor());
        return true;
    }

    bool VisitOverloadExpr(OverloadExpr *expression) {
        for (const auto *candidate : expression->decls()) {
            record(expression->getNameLoc(), "candidate", candidate);
        }
        return true;
    }

    bool VisitGotoStmt(GotoStmt *statement) {
        record(statement->getLabelLoc(), "ref", statement->getLabel());
        return true;
    }

    bool VisitAddrLabelExpr(AddrLabelExpr *expression) {
        record(expression->getLabelLoc(), "ref", expression->getLabel());
        return true;
    }

    bool VisitDesignatedInitExpr(DesignatedInitExpr *expression) {
        for (const auto &designator : expression->designators()) {
            if (designator.isFieldDesignator()) {
                record(designator.getFieldLoc(), "ref", designator.getFieldDecl());
            }
        }
        return true;
    }

    bool VisitOffsetOfExpr(OffsetOfExpr *expression) {
        for (unsigned index = 0; index < expression->getNumComponents(); ++index) {
            const auto &component = expression->getComponent(index);
            if (component.getKind() == OffsetOfNode::Field) {
                record(component.getEndLoc(), "ref", component.getField());
            }
        }
        return true;
    }

    bool VisitConceptSpecializationExpr(ConceptSpecializationExpr *expression) {
        record(expression->getConceptNameLoc(), "ref", expression->getNamedConcept());
        return true;
    }

    bool TraverseConceptReference(ConceptReference *reference) {
        if (reference) {
            record(reference->getConceptNameLoc(), "ref", reference->getNamedConcept());
        }
        return Base::TraverseConceptReference(reference);
    }

    bool TraverseLambdaCapture(LambdaExpr *lambda, const LambdaCapture *capture, Expr *initializer) {
        if (capture->capturesVariable() && capture->isExplicit() && !capture->getCapturedVar()->isInitCapture()) {
            record(capture->getLocation(), "capture", capture->getCapturedVar());
        }
        return Base::TraverseLambdaCapture(lambda, capture, initializer);
    }

    bool TraverseConstructorInitializer(CXXCtorInitializer *initializer) {
        if (initializer->isAnyMemberInitializer() && initializer->isWritten()) {
            record(initializer->getMemberLocation(), "ref", initializer->getAnyMember());
        }
        return Base::TraverseConstructorInitializer(initializer);
    }

    bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc specifier) {
        if (specifier) {
            auto *nested = specifier.getNestedNameSpecifier();
            if (nested->getKind() == NestedNameSpecifier::Namespace) {
                record(specifier.getLocalBeginLoc(), "ref", nested->getAsNamespace());
            } else if (nested->getKind() == NestedNameSpecifier::NamespaceAlias) {
                record(specifier.getLocalBeginLoc(), "ref", nested->getAsNamespaceAlias());
            }
        }
        return Base::TraverseNestedNameSpecifierLoc(specifier);
    }

    bool TraverseTemplateArgumentLoc(const TemplateArgumentLoc &argument) {
        auto kind = argument.getArgument().getKind();
        if (kind == TemplateArgument::Template || kind == TemplateArgument::TemplateExpansion) {
            if (auto *named = argument.getArgument().getAsTemplateOrTemplatePattern().getAsTemplateDecl()) {
                record(argument.getTemplateNameLoc(), "ref", named);
            }
        }
        return Base::TraverseTemplateArgumentLoc(argument);
    }

    bool VisitTagTypeLoc(TagTypeLoc type) {
        record(type.getNameLoc(), "type", type.getDecl());
        return true;
    }

    bool VisitTypedefTypeLoc(TypedefTypeLoc type) {
        record(type.getNameLoc(), "type", type.getTypedefNameDecl());
        return true;
    }

    bool VisitUsingTypeLoc(UsingTypeLoc type) {
        record(type.getNameLoc(), "type", type.getFoundDecl());
        return true;
    }

    bool VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc type) {
        record(type.getNameLoc(), "type", type.getDecl());
        return true;
    }

    bool VisitInjectedClassNameTypeLoc(InjectedClassNameTypeLoc type) {
        record(type.getNameLoc(), "type", type.getDecl());
        return true;
    }

    bool VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc type) {
        record(type.getTemplateNameLoc(), "type", type.getTypePtr()->getTemplateName().getAsTemplateDecl());
        return true;
    }

    bool VisitDeducedTemplateSpecializationTypeLoc(DeducedTemplateSpecializationTypeLoc type) {
        record(type.getTemplateNameLoc(), "type", type.getTypePtr()->getTemplateName().getAsTemplateDecl());
        return true;
    }

  private:
    void record(SourceLocation location, llvm::StringRef role, const NamedDecl *declaration) {
        if (!declaration) {
            return;
        }
        auto normalized = normalize(sourceManager_, location);
        if (!normalized) {
            return;
        }
        bool inWorkspace = workspace_.contains(normalized->location.file);
        if (inWorkspace) {
            out_.workspaceFiles.insert(normalized->location.file);
            auto &named = normalized->macroSuffix.empty() ? out_.namedAtToken : out_.namedAtTokenInMacro;
            auto &atToken = named[normalized->location];
            if (std::find(atToken.begin(), atToken.end(), declaration) == atToken.end()) {
                atToken.push_back(declaration);
            }
        } else if (instantiationDepth_ == 0) {
            return;
        }
        out_.bindings[BindingKey{normalized->location, role.str() + normalized->macroSuffix}].insert(
            identityOf(declaration));
    }

    const Workspace &workspace_;
    const SourceManager &sourceManager_;
    TranslationUnitBindings &out_;
    int instantiationDepth_ = 0;
};

void collectPreprocessorFacts(const Workspace &workspace, ASTUnit &unit, TranslationUnitBindings &out) {
    auto &preprocessor = unit.getPreprocessor();
    const auto &sourceManager = unit.getSourceManager();
    for (auto it = preprocessor.macro_begin(); it != preprocessor.macro_end(); ++it) {
        out.macroNames.insert(it->first->getName().str());
    }
    if (auto *record = preprocessor.getPreprocessingRecord()) {
        for (const auto &range : record->getSkippedRanges()) {
            auto begin = fileOffsetOf(sourceManager, sourceManager.getExpansionLoc(range.getBegin()));
            auto end = fileOffsetOf(sourceManager, sourceManager.getExpansionLoc(range.getEnd()));
            if (begin && end && begin->file == end->file && workspace.contains(begin->file)) {
                out.skippedRanges[begin->file].emplace_back(begin->offset, end->offset);
            }
        }
    }
    for (auto it = sourceManager.fileinfo_begin(); it != sourceManager.fileinfo_end(); ++it) {
        llvm::SmallString<256> path(it->first.getName());
        sourceManager.getFileManager().makeAbsolutePath(path);
        llvm::sys::path::remove_dots(path, true);
        if (workspace.contains(path)) {
            out.workspaceFiles.insert(std::string(path));
        }
    }
}

} // namespace

std::vector<TranslationUnitBindings> collectBindings(const Workspace &workspace, ParsedWorkspace &parsed) {
    std::vector<TranslationUnitBindings> result;
    for (auto &unit : parsed.units) {
        TranslationUnitBindings bindings;
        bindings.unit = unit.get();
        bindings.mainFile = normalizedAbsolutePath(unit->getMainFileName(), workspace.root());
        BindingVisitor visitor(workspace, unit->getASTContext(), bindings);
        visitor.TraverseDecl(unit->getASTContext().getTranslationUnitDecl());
        collectPreprocessorFacts(workspace, *unit, bindings);
        result.push_back(std::move(bindings));
    }
    return result;
}

} // namespace riktig
