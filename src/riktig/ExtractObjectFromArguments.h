#pragma once

#include "FunctionRefactoring.h"
#include "Protocol.h"
#include "Results.h"
#include "Workspace.h"

#include "clang/AST/Decl.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace riktig {

struct ExtractObjectFromArgumentsRequest {
    std::string file; // relative to the workspace root, or absolute
    Position at;
    std::vector<std::string> parameters;
    std::string typeName;
    std::string parameterName; // empty for the type's name in lowerCamelCase
};

RefactoringResult extractObjectFromArguments(const Workspace &workspace,
                                             const ExtractObjectFromArgumentsRequest &request);

// The steps of Extract object from arguments, exposed so the tests' deliberately naive implementation can skip the
// checks.
class ExtractObjectPlanner {
  public:
    ExtractObjectPlanner(const Workspace &workspace, ExtractObjectFromArgumentsRequest request);

    std::optional<RefusalReason> parseAndCollect();
    std::optional<RefusalReason> findFunctionAndParameters();
    // Records the first precondition that the function, a parameter or a call breaks.
    void checkPreconditions();
    const std::optional<RefusalReason> &firstBrokenPrecondition() const { return brokenPrecondition_; }
    std::optional<RefusalReason> writeEdits();
    std::optional<RefusalReason> verify();

    const EditPlan &plan() const { return plan_; }

  private:
    void breakPrecondition(const char *code, std::string message, const clang::ASTContext *context = nullptr,
                           clang::SourceLocation location = {}, std::string suggestion = {});
    std::optional<FileRange> rangeAt(const clang::ASTContext &context, clang::SourceLocation location) const;
    std::optional<RefusalReason> writeTypeDeclaration();
    std::optional<RefusalReason> rewriteDeclaration(const clang::FunctionDecl *declaration);
    std::optional<RefusalReason> rewriteBody(const clang::FunctionDecl *definition);
    std::optional<RefusalReason> rewriteCall(const Reference &reference);

    struct InsertedText {
        std::string file;
        unsigned replacementOffset = 0;
        unsigned replacementLength = 0;
        unsigned positionInNewText = 0;
    };
    unsigned afterOffsetOf(const InsertedText &inserted) const;

    const Workspace &workspace_;
    ExtractObjectFromArgumentsRequest request_;
    LoadedWorkspace loaded_;
    const clang::FunctionDecl *function_ = nullptr;
    unsigned firstGrouped_ = 0;
    unsigned groupedCount_ = 0;
    bool everyGroupedHasDefault_ = false;
    std::vector<const clang::FunctionDecl *> declarations_;
    std::vector<Reference> references_;
    std::optional<RefusalReason> brokenPrecondition_;
    EditPlan plan_;
    // Where the new type's name, its members' names, the new parameters' names and the uses of the new parameter
    // in the body are in the inserted text.
    std::optional<InsertedText> typeName_;
    std::vector<InsertedText> memberNames_;
    std::vector<InsertedText> parameterNames_;
    std::optional<InsertedText> definitionParameterName_;
    struct MemberName {
        InsertedText text;
        unsigned memberIndex = 0;
    };
    // Uses of the new parameter in the body (`line` in `line.price`), and member names in them and in designators.
    std::vector<InsertedText> parameterUses_;
    std::vector<MemberName> memberUses_;
    std::vector<InsertedText> typeUses_;
};

} // namespace riktig
