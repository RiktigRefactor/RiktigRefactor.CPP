#include "ExtractObjectFromArguments.h"

#include "Independence.h"
#include "NameChecks.h"
#include "RawTokens.h"
#include "RefusalCodes.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

namespace riktig {

using namespace clang;
namespace codes = refusal_codes;

namespace {

class GroupedParameterUseFinder : public RecursiveASTVisitor<GroupedParameterUseFinder> {
  public:
    explicit GroupedParameterUseFinder(std::vector<const ParmVarDecl *> parameters)
        : parameters_(std::move(parameters)) {}

    bool VisitDeclRefExpr(DeclRefExpr *reference) {
        for (unsigned index = 0; index < parameters_.size(); ++index) {
            if (reference->getDecl() == parameters_[index]) {
                uses.push_back({reference, index});
            }
        }
        return true;
    }
    bool VisitLambdaExpr(LambdaExpr *lambda) {
        for (const auto &capture : lambda->captures()) {
            if (capture.capturesVariable() && capture.isExplicit() &&
                llvm::is_contained(parameters_, capture.getCapturedVar()) && !explicitCapture) {
                explicitCapture = lambda;
            }
        }
        return true;
    }

    struct Use {
        const DeclRefExpr *reference;
        unsigned index;
    };
    std::vector<Use> uses;
    const LambdaExpr *explicitCapture = nullptr;

  private:
    std::vector<const ParmVarDecl *> parameters_;
};

std::string lowerCamelCase(const std::string &name) {
    std::string result = name;
    if (!result.empty()) {
        result[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[0])));
    }
    return result;
}

} // namespace

ExtractObjectPlanner::ExtractObjectPlanner(const Workspace &workspace, ExtractObjectFromArgumentsRequest request)
    : workspace_(workspace), request_(std::move(request)), plan_(workspace) {
    if (request_.parameterName.empty()) {
        request_.parameterName = lowerCamelCase(request_.typeName);
    }
}

std::optional<FileRange> ExtractObjectPlanner::rangeAt(const ASTContext &context, SourceLocation location) const {
    auto offset = fileOffsetOf(context.getSourceManager(), context.getSourceManager().getExpansionLoc(location));
    if (!offset) {
        return std::nullopt;
    }
    return rangeOf(workspace_, *offset);
}

void ExtractObjectPlanner::breakPrecondition(const char *code, std::string message, const ASTContext *context,
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

unsigned ExtractObjectPlanner::afterOffsetOf(const InsertedText &inserted) const {
    return plan_.afterOffset(inserted.file, inserted.replacementOffset, inserted.replacementLength,
                             inserted.positionInNewText);
}

std::optional<RefusalReason> ExtractObjectPlanner::parseAndCollect() { return loadWorkspace(workspace_, loaded_); }

std::optional<RefusalReason> ExtractObjectPlanner::findFunctionAndParameters() {
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
        return makeRefusal(codes::NoSymbol,
                           "`" + declaration->getNameAsString() + "` is not a function named by an identifier.",
                           rangeAt(context, declaration->getLocation()));
    }
    if (request_.parameters.empty()) {
        return makeRefusal(codes::InvalidParameter, "Name at least one parameter to group.");
    }
    std::vector<unsigned> indices;
    for (const auto &name : request_.parameters) {
        std::optional<unsigned> found;
        for (unsigned index = 0; index < function->getNumParams(); ++index) {
            if (function->getParamDecl(index)->getName() == name) {
                found = index;
            }
        }
        if (!found) {
            return makeRefusal(codes::InvalidParameter,
                               "`" + function->getNameAsString() + "` has no parameter named `" + name + "`.",
                               rangeAt(context, function->getLocation()));
        }
        indices.push_back(*found);
    }
    std::sort(indices.begin(), indices.end());
    if (std::adjacent_find(indices.begin(), indices.end()) != indices.end()) {
        return makeRefusal(codes::InvalidParameter, "A parameter is named twice.");
    }
    if (indices.back() - indices.front() + 1 != indices.size()) {
        return makeRefusal(codes::NonContiguousParameters,
                           "The tool groups only parameters that are next to each other.",
                           rangeAt(context, function->getLocation()), "Reorder the parameters first.");
    }
    firstGrouped_ = indices.front();
    groupedCount_ = indices.size();
    function_ = cast<FunctionDecl>(function->getCanonicalDecl());
    for (const auto *found : declarationsOf(loaded_, function_)) {
        declarations_.push_back(cast<FunctionDecl>(found));
    }
    references_ = findReferences(workspace_, loaded_, function_);
    return std::nullopt;
}

void ExtractObjectPlanner::checkPreconditions() {
    const auto &context = function_->getASTContext();
    auto name = function_->getNameAsString();
    auto location = function_->getLocation();
    if (auto refusal = validateNewName("", request_.typeName)) {
        brokenPrecondition_ = brokenPrecondition_ ? brokenPrecondition_ : refusal;
    }
    if (auto refusal = validateNewName("", request_.parameterName)) {
        brokenPrecondition_ = brokenPrecondition_ ? brokenPrecondition_ : refusal;
    }
    auto identity = identityOf(function_);
    if (identity.location.file.empty() || !workspace_.contains(identity.location.file)) {
        breakPrecondition(codes::DeclaredOutsideWorkspace, "`" + name + "` is declared outside the workspace.");
    }
    if (identity.tag.find('@') != std::string::npos) {
        breakPrecondition(codes::MacroInvolved, "`" + name + "` is declared by a macro.", &context, location);
    }
    if (function_->isMain() || function_->isExternC() || function_->isVariadic() || function_->isDeleted()) {
        breakPrecondition(codes::UnsupportedMethod,
                          "`" + name + "` is `main`, has C linkage, is variadic or is deleted.", &context, location);
    }
    if (const auto *method = dyn_cast<CXXMethodDecl>(function_)) {
        if (method->isVirtual() || method->size_overridden_methods() > 0) {
            breakPrecondition(codes::VirtualDispatch,
                              "`" + name + "` is virtual; the tool does not change a hierarchy of overrides.", &context,
                              location);
        }
        if (method->getParent()->isDependentContext() || isa<ClassTemplateSpecializationDecl>(method->getParent())) {
            breakPrecondition(codes::TemplateInvolved, "`" + name + "` is a member of a template.", &context, location);
        }
        if (isa<CXXConstructorDecl>(method) || method->isOverloadedOperator()) {
            breakPrecondition(codes::UnsupportedMethod, "`" + name + "` is a constructor or operator.", &context,
                              location);
        }
    }
    if (function_->getTemplatedKind() != FunctionDecl::TK_NonTemplate) {
        breakPrecondition(codes::TemplateInvolved, "`" + name + "` is a template.", &context, location);
    }
    bool everyHasDefault = true;
    for (unsigned index = firstGrouped_; index < firstGrouped_ + groupedCount_; ++index) {
        const auto *parameter = function_->getParamDecl(index);
        auto type = parameter->getType();
        if (type->isReferenceType() || type->isArrayType() || type.isVolatileQualified() ||
            type->isFunctionPointerType() || parameter->isParameterPack()) {
            breakPrecondition(codes::UnsupportedParameter,
                              "`" + parameter->getNameAsString() +
                                  "` is a reference, array, function pointer or volatile parameter, which the tool "
                                  "does not make a member.",
                              &context, parameter->getLocation());
        }
        if (const auto *record = type->getAsCXXRecordDecl();
            record && record->hasDefinition() && !record->hasTrivialDestructor()) {
            breakPrecondition(codes::UnsupportedParameter,
                              "`" + parameter->getNameAsString() +
                                  "` has a non-trivial destructor, which would run at another time as a member.",
                              &context, parameter->getLocation());
        }
        everyHasDefault = everyHasDefault && parameter->hasDefaultArg();
    }
    everyGroupedHasDefault_ = everyHasDefault;
    for (const auto *declaration : declarations_) {
        const auto &declarationContext = declaration->getASTContext();
        if (declaration->getFriendObjectKind() != Decl::FOK_None) {
            breakPrecondition(codes::UnsupportedMethod, "A friend declaration names `" + name + "`.",
                              &declarationContext, declaration->getLocation());
        }
        for (unsigned index = firstGrouped_; index < firstGrouped_ + groupedCount_; ++index) {
            const auto *parameter = declaration->getParamDecl(index);
            if (parameter->hasDefaultArg() && !parameter->hasUninstantiatedDefaultArg() &&
                !parameter->getDefaultArg()->isEvaluatable(declarationContext)) {
                breakPrecondition(codes::UnsupportedDefault,
                                  "The default argument of `" + parameter->getNameAsString() +
                                      "` is not a constant; as a default member initializer it would look names up "
                                      "in another scope.",
                                  &declarationContext, parameter->getLocation());
            }
            if (parameter->getName().empty() && declaration->doesThisDeclarationHaveABody()) {
                breakPrecondition(codes::UnsupportedParameter, "A grouped parameter of the definition is unnamed.",
                                  &declarationContext, parameter->getLocation());
            }
        }
        if (auto range = fileRangeOf(declarationContext, declaration->getSourceRange())) {
            auto text = plan_.text(range->file).substr(range->begin, range->end - range->begin);
            forEachRawIdentifier(text, [&](const RawIdentifier &identifier) {
                if (identifier.text == request_.parameterName) {
                    breakPrecondition(
                        codes::NameCollision, "`" + request_.parameterName + "` is already used in `" + name + "`.",
                        &declarationContext, declaration->getLocation(), "Choose another parameter name.");
                }
            });
        }
        if (declaration->doesThisDeclarationHaveABody()) {
            std::vector<const ParmVarDecl *> grouped;
            for (unsigned index = firstGrouped_; index < firstGrouped_ + groupedCount_; ++index) {
                grouped.push_back(declaration->getParamDecl(index));
            }
            GroupedParameterUseFinder finder(grouped);
            finder.TraverseStmt(declaration->getBody());
            if (finder.explicitCapture) {
                breakPrecondition(codes::CapturedVariable, "A lambda captures a grouped parameter explicitly.",
                                  &declarationContext, finder.explicitCapture->getBeginLoc());
            }
        }
    }
    // The new type's name must be new everywhere, so declaring it captures no other use.
    std::set<std::string> files;
    for (const auto &unit : loaded_.units) {
        files.insert(unit.workspaceFiles.begin(), unit.workspaceFiles.end());
        if (unit.macroNames.count(request_.typeName) || unit.macroNames.count(request_.parameterName) ||
            unit.macroNames.count(name)) {
            breakPrecondition(codes::MacroInvolved, "A new name or the function's name is also a macro.");
        }
    }
    for (const auto &file : files) {
        forEachRawIdentifier(plan_.text(file), [&](const RawIdentifier &identifier) {
            if (identifier.text == request_.typeName) {
                breakPrecondition(codes::NameCollision,
                                  "`" + request_.typeName + "` is already used in " + workspace_.displayPath(file) +
                                      ".",
                                  nullptr, {}, "Choose another type name.");
            }
        });
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
        auto arguments = writtenArguments(reference.call);
        IndependenceAnalysis independence(*reference.context, reference.enclosingFunction);
        for (unsigned grouped = firstGrouped_; grouped < firstGrouped_ + groupedCount_ && grouped < arguments.size();
             ++grouped) {
            for (unsigned other = 0; other < arguments.size(); ++other) {
                if (other != grouped && !independence.areIndependent(arguments[grouped], arguments[other])) {
                    breakPrecondition(codes::EvaluationOrder,
                                      "Function arguments are evaluated in any order, but the new object's members "
                                      "in order, and the tool can't show these arguments are independent.",
                                      &referenceContext, arguments[grouped]->getBeginLoc(),
                                      "Store the arguments in local variables first.");
                    other = arguments.size();
                    grouped = arguments.size();
                }
            }
        }
    }
}

std::optional<RefusalReason> ExtractObjectPlanner::writeTypeDeclaration() {
    // The type goes before the first declaration of the function, or of the outermost class holding it.
    const FunctionDecl *first = function_;
    const auto &context = first->getASTContext();
    const Decl *anchor = first;
    for (const DeclContext *enclosing = first->getLexicalDeclContext(); enclosing && !enclosing->isFileContext();
         enclosing = enclosing->getLexicalParent()) {
        if (const auto *record = dyn_cast<CXXRecordDecl>(enclosing)) {
            anchor = record;
        } else {
            return makeRefusal(codes::UnsupportedMethod, "The function is declared in a local class.",
                               rangeAt(context, first->getLocation()));
        }
    }
    auto begin = fileOffsetOfLocation(context, anchor->getBeginLoc());
    if (!begin) {
        return makeRefusal(codes::MacroInvolved, "The declaration is written in a macro.",
                           rangeAt(context, first->getLocation()));
    }
    const auto &text = plan_.text(begin->file);
    unsigned lineStart = begin->offset;
    while (lineStart > 0 && (text[lineStart - 1] == ' ' || text[lineStart - 1] == '\t')) {
        --lineStart;
    }
    if (lineStart > 0 && text[lineStart - 1] != '\n') {
        return makeRefusal(codes::UnsupportedMethod, "The declaration does not start a line.",
                           rangeAt(context, first->getLocation()));
    }
    std::string indentation = text.substr(lineStart, begin->offset - lineStart);
    std::string declaration = indentation + "struct ";
    typeName_ = InsertedText{begin->file, lineStart, 0, static_cast<unsigned>(declaration.size())};
    declaration += request_.typeName + " {\n";
    struct Copy {
        OffsetRange range;
        unsigned position;
    };
    std::vector<Copy> copies;
    for (unsigned index = firstGrouped_; index < firstGrouped_ + groupedCount_; ++index) {
        const auto *parameter = first->getParamDecl(index);
        auto typeRange = fileRangeOf(context, parameter->getTypeSourceInfo()->getTypeLoc().getSourceRange());
        auto nameLocation = fileOffsetOfLocation(context, parameter->getLocation());
        if (!typeRange || typeRange->file != begin->file) {
            return makeRefusal(codes::MacroInvolved, "A parameter's type is written in a macro.",
                               rangeAt(context, parameter->getLocation()));
        }
        if (nameLocation && nameLocation->offset < typeRange->end) {
            return makeRefusal(codes::UnsupportedParameter, "A parameter's name is inside its type.",
                               rangeAt(context, parameter->getLocation()));
        }
        declaration += indentation + "    ";
        copies.push_back({*typeRange, static_cast<unsigned>(declaration.size())});
        declaration += textOf(plan_, *typeRange) + " ";
        memberNames_.push_back(InsertedText{begin->file, lineStart, 0, static_cast<unsigned>(declaration.size())});
        declaration += first->getParamDecl(index)->getName().str();
        // A default argument may be on a later declaration.
        const ParmVarDecl *withDefault = nullptr;
        for (const auto *redeclaration : declarations_) {
            if (redeclaration->getParamDecl(index)->hasDefaultArg() && &redeclaration->getASTContext() == &context) {
                withDefault = redeclaration->getParamDecl(index);
            }
        }
        if (withDefault) {
            auto defaultRange = fileRangeOf(context, withDefault->getDefaultArgRange());
            if (!defaultRange) {
                return makeRefusal(codes::MacroInvolved, "A default argument is written in a macro.",
                                   rangeAt(context, withDefault->getLocation()));
            }
            declaration += " = ";
            copies.push_back({*defaultRange, static_cast<unsigned>(declaration.size())});
            declaration += textOf(plan_, *defaultRange);
        }
        declaration += ";\n";
    }
    declaration += indentation + "};\n\n";
    if (!plan_.insert(begin->file, lineStart, declaration)) {
        return makeRefusal(codes::UnsupportedReference, "The new type overlaps another edit.");
    }
    for (const auto &copy : copies) {
        plan_.relocate(copy.range.file, copy.range.begin, copy.range.end - copy.range.begin, begin->file, lineStart, 0,
                       copy.position);
    }
    return std::nullopt;
}

std::optional<RefusalReason> ExtractObjectPlanner::rewriteDeclaration(const FunctionDecl *declaration) {
    const auto &context = declaration->getASTContext();
    const auto *firstParameter = declaration->getParamDecl(firstGrouped_);
    const auto *lastParameter = declaration->getParamDecl(firstGrouped_ + groupedCount_ - 1);
    auto first = fileRangeOf(context, firstParameter->getSourceRange());
    auto last = fileRangeOf(context, lastParameter->getSourceRange());
    if (!first || !last || first->file != last->file) {
        return makeRefusal(codes::MacroInvolved, "A parameter is written in a macro.",
                           rangeAt(context, declaration->getLocation()));
    }
    std::string replacement = request_.typeName + " " + request_.parameterName;
    bool hasDefault = false;
    for (unsigned index = firstGrouped_; index < firstGrouped_ + groupedCount_; ++index) {
        hasDefault = hasDefault || declaration->getParamDecl(index)->hasDefaultArg();
    }
    if (hasDefault && everyGroupedHasDefault_) {
        replacement += " = {}";
    }
    if (!plan_.replace(first->file, first->begin, last->end - first->begin, replacement)) {
        return makeRefusal(codes::UnsupportedReference, "The parameter list overlaps another edit.",
                           rangeAt(context, declaration->getLocation()));
    }
    typeUses_.push_back(InsertedText{first->file, first->begin, last->end - first->begin, 0});
    InsertedText name{first->file, first->begin, last->end - first->begin,
                      static_cast<unsigned>(request_.typeName.size() + 1)};
    parameterNames_.push_back(name);
    if (declaration->doesThisDeclarationHaveABody()) {
        definitionParameterName_ = name;
    }
    return std::nullopt;
}

std::optional<RefusalReason> ExtractObjectPlanner::rewriteBody(const FunctionDecl *definition) {
    auto &context = definition->getASTContext();
    std::vector<const ParmVarDecl *> grouped;
    for (unsigned index = firstGrouped_; index < firstGrouped_ + groupedCount_; ++index) {
        grouped.push_back(definition->getParamDecl(index));
    }
    GroupedParameterUseFinder finder(grouped);
    finder.TraverseStmt(definition->getBody());
    for (const auto &use : finder.uses) {
        auto range = fileRangeOf(context, use.reference->getSourceRange());
        if (!range) {
            return makeRefusal(codes::MacroInvolved, "A grouped parameter is used in a macro.",
                               rangeAt(context, use.reference->getLocation()));
        }
        auto memberName = grouped[use.index]->getName().str();
        if (!plan_.replace(range->file, range->begin, range->end - range->begin,
                           request_.parameterName + "." + memberName)) {
            return makeRefusal(codes::UnsupportedReference, "A use of a parameter overlaps another edit.",
                               rangeAt(context, use.reference->getLocation()));
        }
        parameterUses_.push_back(InsertedText{range->file, range->begin, range->end - range->begin, 0});
        memberUses_.push_back(MemberName{InsertedText{range->file, range->begin, range->end - range->begin,
                                                      static_cast<unsigned>(request_.parameterName.size() + 1)},
                                         use.index});
    }
    return std::nullopt;
}

std::optional<RefusalReason> ExtractObjectPlanner::rewriteCall(const Reference &reference) {
    const auto &context = *reference.context;
    auto arguments = writtenArguments(reference.call);
    if (arguments.size() <= firstGrouped_) {
        return std::nullopt; // every grouped argument is a default, which the new parameter's default gives
    }
    unsigned lastGiven = std::min<unsigned>(arguments.size(), firstGrouped_ + groupedCount_) - 1;
    for (unsigned index = firstGrouped_; index <= lastGiven; ++index) {
        auto range = fileRangeOf(context, arguments[index]->getSourceRange());
        if (!range) {
            return makeRefusal(codes::MacroInvolved, "An argument is written in a macro.",
                               rangeAt(context, arguments[index]->getBeginLoc()));
        }
        if (isa<InitListExpr>(arguments[index]->IgnoreImplicit())) {
            return makeRefusal(codes::UnsupportedReference,
                               "An argument is a braced list, which would initialize a member differently.",
                               rangeAt(context, arguments[index]->getBeginLoc()));
        }
        auto memberName = function_->getParamDecl(index)->getName().str();
        std::string prefix = index == firstGrouped_ ? request_.typeName + "{" : "";
        unsigned designatorPosition = prefix.size() + 1;
        prefix += "." + memberName + " = ";
        if (!plan_.insert(range->file, range->begin, prefix) ||
            (index == lastGiven && !plan_.insert(range->file, range->end, "}"))) {
            return makeRefusal(codes::UnsupportedReference, "A call's arguments overlap another edit.",
                               rangeAt(context, arguments[index]->getBeginLoc()));
        }
        if (index == firstGrouped_) {
            typeUses_.push_back(InsertedText{range->file, range->begin, 0, 0});
        }
        memberUses_.push_back(
            MemberName{InsertedText{range->file, range->begin, 0, designatorPosition}, index - firstGrouped_});
    }
    return std::nullopt;
}

std::optional<RefusalReason> ExtractObjectPlanner::writeEdits() {
    if (auto refusal = writeTypeDeclaration()) {
        return refusal;
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
    for (const auto &reference : references_) {
        if (!reference.call || reference.inTemplateInstantiation || !reference.inWorkspace || reference.inMacro) {
            continue;
        }
        if (auto refusal = rewriteCall(reference)) {
            return refusal;
        }
    }
    // What the inserted names must bind to, now that every edit is known.
    DeclIdentity type{{typeName_->file, afterOffsetOf(*typeName_)}, "CXXRecord"};
    plan_.expectBinding(typeName_->file, typeName_->replacementOffset, 0, typeName_->positionInNewText, "decl", type,
                        true);
    std::vector<DeclIdentity> members;
    for (const auto &member : memberNames_) {
        members.push_back(DeclIdentity{{member.file, afterOffsetOf(member)}, "Field"});
        plan_.expectBinding(member.file, member.replacementOffset, 0, member.positionInNewText, "decl", members.back(),
                            true);
    }
    for (const auto &use : typeUses_) {
        plan_.expectBinding(use.file, use.replacementOffset, use.replacementLength, use.positionInNewText, "type", type,
                            true);
    }
    for (const auto &name : parameterNames_) {
        plan_.expectBinding(name.file, name.replacementOffset, name.replacementLength, name.positionInNewText, "decl",
                            DeclIdentity{{name.file, afterOffsetOf(name)}, "ParmVar"}, true);
    }
    if (definitionParameterName_) {
        DeclIdentity parameter{{definitionParameterName_->file, afterOffsetOf(*definitionParameterName_)}, "ParmVar"};
        for (const auto &use : parameterUses_) {
            plan_.expectBinding(use.file, use.replacementOffset, use.replacementLength, use.positionInNewText, "ref",
                                parameter, true);
        }
    }
    for (const auto &use : memberUses_) {
        plan_.expectBinding(use.text.file, use.text.replacementOffset, use.text.replacementLength,
                            use.text.positionInNewText, "ref", members[use.memberIndex], true);
    }
    return std::nullopt;
}

std::optional<RefusalReason> ExtractObjectPlanner::verify() {
    BindingExpectations intended;
    // The new type's implicit members are declared at its name, and closures that captured a grouped parameter now
    // capture the new one, whose implicit member is declared at its first use.
    intended.isNew = [&](const BindingKey &key) {
        if (key.location.file == typeName_->file && key.location.offset == afterOffsetOf(*typeName_)) {
            return true;
        }
        if (key.role == "decl" || key.role == "type") {
            for (const auto &use : parameterUses_) {
                if (key.location.file == use.file && key.location.offset == afterOffsetOf(use)) {
                    return true;
                }
            }
        }
        return false;
    };
    return verifyEditPlan(workspace_, loaded_, plan_, intended, "extracting the object");
}

RefactoringResult extractObjectFromArguments(const Workspace &workspace,
                                             const ExtractObjectFromArgumentsRequest &request) {
    auto refuse = [&](RefusalReason reason) { return RefactoringResult::refused(workspace.root(), std::move(reason)); };
    ExtractObjectPlanner planner(workspace, request);
    if (auto refusal = planner.parseAndCollect()) {
        return refuse(*refusal);
    }
    if (auto refusal = planner.findFunctionAndParameters()) {
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
