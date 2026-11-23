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

struct MakeMethodStaticRequest {
    std::string file; // relative to the workspace root, or absolute
    Position at;
    std::string receiverName = "self";
    std::string pass = "receiver";
};

RefactoringResult makeMethodStatic(const Workspace &workspace, const MakeMethodStaticRequest &request);

// The steps of Make method static, exposed so the tests' deliberately naive implementation can skip the checks.
class MakeMethodStaticPlanner {
  public:
    MakeMethodStaticPlanner(const Workspace &workspace, MakeMethodStaticRequest request);

    std::optional<RefusalReason> parseAndCollect();
    std::optional<RefusalReason> findMethod();
    // Records the first precondition that the method or a call site breaks.
    void checkPreconditions();
    const std::optional<RefusalReason> &firstBrokenPrecondition() const { return brokenPrecondition_; }
    std::optional<RefusalReason> writeEdits();
    std::optional<RefusalReason> verify();

    const EditPlan &plan() const { return plan_; }

  private:
    void breakPrecondition(const char *code, std::string message, const clang::ASTContext *context = nullptr,
                           clang::SourceLocation location = {}, std::string suggestion = {});
    std::optional<RefusalReason> rewriteDeclaration(const clang::CXXMethodDecl *declaration);
    std::optional<RefusalReason> rewriteBody(const clang::CXXMethodDecl *definition);
    std::optional<RefusalReason> rewriteCall(const FunctionReference &reference);
    bool isInBodyOfMethod(const FunctionReference &reference) const;
    std::optional<FileRange> rangeAt(const clang::ASTContext &context, clang::SourceLocation location) const;

    const Workspace &workspace_;
    MakeMethodStaticRequest request_;
    LoadedWorkspace loaded_;
    const clang::CXXMethodDecl *method_ = nullptr;
    const clang::CXXRecordDecl *record_ = nullptr;
    std::vector<const clang::FunctionDecl *> declarations_;
    std::vector<FunctionReference> references_;
    std::optional<RefusalReason> brokenPrecondition_;
    EditPlan plan_;
    // Receivers of calls in the method's own body: `this`, which the call's rewrite replaces with the receiver
    // parameter, or another expression, whose text moves.
    struct ReceiverInBody {
        OffsetRange range;
        bool moves = false;
    };
    std::vector<ReceiverInBody> receiversInBody_;
    struct InsertedName {
        std::string file;
        unsigned replacementOffset = 0;
        unsigned replacementLength = 0;
        unsigned positionInNewText = 0;
    };
    std::vector<InsertedName> parameterNames_;
    std::optional<InsertedName> definitionParameter_;
    std::vector<InsertedName> receiverUsesInBody_;
    std::vector<FileOffset> implicitCaptureDefaults_;
    std::vector<OffsetRange> lambdasCapturingThis_;
    // Where the closure members that held `this` are declared; they hold the receiver parameter after the change.
    std::set<FileOffset> closureMembersCapturingThis_;
};

} // namespace riktig
