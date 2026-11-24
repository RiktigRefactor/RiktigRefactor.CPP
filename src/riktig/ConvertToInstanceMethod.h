#pragma once

#include "FunctionRefactoring.h"
#include "Protocol.h"
#include "Results.h"
#include "Workspace.h"

#include "clang/AST/DeclCXX.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace riktig {

struct ConvertToInstanceMethodRequest {
    std::string file; // relative to the workspace root, or absolute
    Position at;
    std::string receiver; // the name of the parameter that becomes the receiver
};

RefactoringResult convertToInstanceMethod(const Workspace &workspace, const ConvertToInstanceMethodRequest &request);

// The steps of Convert static method to instance method, exposed so the tests' deliberately naive implementation can
// skip the checks.
class ConvertToInstanceMethodPlanner {
  public:
    ConvertToInstanceMethodPlanner(const Workspace &workspace, ConvertToInstanceMethodRequest request);

    std::optional<RefusalReason> parseAndCollect();
    std::optional<RefusalReason> findMethodAndReceiver();
    // Records the first precondition that the method, the receiver parameter or a call breaks.
    void checkPreconditions();
    const std::optional<RefusalReason> &firstBrokenPrecondition() const { return brokenPrecondition_; }
    std::optional<RefusalReason> writeEdits();
    std::optional<RefusalReason> verify();

    const EditPlan &plan() const { return plan_; }

  private:
    void breakPrecondition(const char *code, std::string message, const clang::ASTContext *context = nullptr,
                           clang::SourceLocation location = {}, std::string suggestion = {});
    std::optional<FileRange> rangeAt(const clang::ASTContext &context, clang::SourceLocation location) const;
    std::optional<RefusalReason> rewriteDeclaration(const clang::CXXMethodDecl *declaration);
    std::optional<RefusalReason> rewriteBody(const clang::CXXMethodDecl *definition);
    std::optional<RefusalReason> rewriteCall(const Reference &reference);
    bool isInBody(const clang::ASTContext &context, clang::SourceLocation location) const;
    // The edit for a use of the receiver parameter in the body: `order.price` becomes `price`, `order` `*this`.
    std::optional<Replacement> editForParameterUse(clang::ASTContext &context, const clang::DeclRefExpr *use) const;

    const Workspace &workspace_;
    ConvertToInstanceMethodRequest request_;
    LoadedWorkspace loaded_;
    const clang::CXXMethodDecl *method_ = nullptr;
    unsigned receiverIndex_ = 0;
    bool receiverIsConst_ = false;
    const clang::CXXRecordDecl *receiverClass_ = nullptr;
    std::vector<const clang::CXXMethodDecl *> declarations_;
    std::vector<Reference> references_;
    std::optional<RefusalReason> brokenPrecondition_;
    EditPlan plan_;
    // Receivers of recursive calls in the body that are the receiver parameter itself, which those calls' rewrite
    // replaces.
    std::vector<OffsetRange> receiverParameterRangesInCalls_;
    std::set<FileOffset> usesRewrittenInMovedReceivers_;
};

} // namespace riktig
