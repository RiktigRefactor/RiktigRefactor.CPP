#pragma once

#include "FunctionRefactoring.h"
#include "Protocol.h"
#include "Results.h"
#include "Workspace.h"

#include "clang/AST/DeclCXX.h"

#include <optional>
#include <string>
#include <vector>

namespace riktig {

struct MoveStaticMemberRequest {
    std::string file; // relative to the workspace root, or absolute
    Position at;
    std::string target; // a qualified class or namespace name, such as `shop::Pricing`
};

RefactoringResult moveStaticMember(const Workspace &workspace, const MoveStaticMemberRequest &request);

// The steps of Move static member, exposed so the tests' deliberately naive implementation can skip the checks.
class MoveStaticMemberPlanner {
  public:
    MoveStaticMemberPlanner(const Workspace &workspace, MoveStaticMemberRequest request);

    std::optional<RefusalReason> parseAndCollect();
    std::optional<RefusalReason> findMemberAndTarget();
    // Records the first precondition that the member, the target or a reference breaks.
    void checkPreconditions();
    const std::optional<RefusalReason> &firstBrokenPrecondition() const { return brokenPrecondition_; }
    std::optional<RefusalReason> writeEdits();
    std::optional<RefusalReason> verify();

    const EditPlan &plan() const { return plan_; }

  private:
    void breakPrecondition(const char *code, std::string message, const clang::ASTContext *context = nullptr,
                           clang::SourceLocation location = {}, std::string suggestion = {});
    std::optional<FileRange> rangeAt(const clang::ASTContext &context, clang::SourceLocation location) const;
    // The declaration that moves into the target: the one in the class, or the namespace-scope definition.
    const clang::ValueDecl *movingDeclaration() const;
    std::optional<OffsetRange> movingRange() const;
    bool isInMovingRange(const std::string &file, unsigned offset) const;
    const clang::DeclContext *targetIn(const clang::ASTContext &context) const;

    struct InnerEdit {
        Replacement replacement; // offset relative to the start of the moving range
        std::vector<QualifierComponent> qualifier;
        unsigned qualifierPosition = 0;
    };
    std::optional<RefusalReason> qualifyNamesFromSource(const clang::ValueDecl *declaration, bool moves,
                                                        std::vector<InnerEdit> &innerEdits);

    const Workspace &workspace_;
    MoveStaticMemberRequest request_;
    LoadedWorkspace loaded_;
    const clang::ValueDecl *member_ = nullptr;
    bool memberOfClass_ = false;
    std::vector<const clang::ValueDecl *> declarations_;
    std::vector<Reference> references_;
    // The target in the translation unit of the declaration that moves.
    const clang::DeclContext *target_ = nullptr;
    std::vector<std::string> targetPath_;
    std::optional<RefusalReason> brokenPrecondition_;
    EditPlan plan_;
};

} // namespace riktig
