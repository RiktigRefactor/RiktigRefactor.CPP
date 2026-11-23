#pragma once

#include "Bindings.h"
#include "EditMap.h"
#include "Protocol.h"
#include "Results.h"
#include "Workspace.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"

#include <optional>
#include <string>
#include <vector>

namespace riktig {

struct ExtractIifeRequest {
    std::string file; // relative to the workspace root, or absolute
    Position from;
    Position to; // exclusive
};

RefactoringResult extractIife(const Workspace &workspace, const ExtractIifeRequest &request);

struct IifeParameter {
    const clang::VarDecl *variable = nullptr;
    bool byReference = true;
    std::string declaration;     // such as `int &count`
    unsigned nameOffsetInHeader; // where the parameter's name is in the inserted header
};

// The steps of Extract IIFE, exposed so the tests' deliberately naive implementation can skip the checks.
class IifePlanner {
  public:
    IifePlanner(const Workspace &workspace, ExtractIifeRequest request);

    std::optional<RefusalReason> parseAndCollect();
    // Finds the selected expression or statements, and records every precondition the selection breaks.
    std::optional<RefusalReason> analyzeSelection();
    const std::optional<RefusalReason> &firstBrokenPrecondition() const { return brokenPrecondition_; }
    std::optional<RefusalReason> writeEdits();
    std::optional<RefusalReason> verify();

    const EditMap &edits() const { return edits_; }

    // Implementation details shared with the analysis.
    void breakPrecondition(const char *code, std::string message, clang::SourceLocation location = {},
                           std::string suggestion = {});
    bool isInSelection(clang::SourceLocation location) const;

  private:
    std::optional<RefusalReason> findSelection();
    void checkContext();
    void analyzeVariables();
    void checkStatementSelection();
    void checkExpressionSelection();
    void checkPreprocessor();
    std::optional<std::string> typeForParameter(const clang::VarDecl *variable, bool byReference);

    const Workspace &workspace_;
    ExtractIifeRequest request_;
    ParsedWorkspace parsed_;
    std::vector<TranslationUnitBindings> translationUnits_;
    TranslationUnitBindings *unit_ = nullptr;
    clang::ASTContext *context_ = nullptr;
    std::string file_;
    std::string text_;
    unsigned start_ = 0;
    unsigned end_ = 0;
    const clang::Expr *expression_ = nullptr;
    const clang::CompoundStmt *block_ = nullptr;
    std::vector<const clang::Stmt *> statements_;
    bool selectionEndsBlock_ = false;
    const clang::Stmt *enclosingBody_ = nullptr;
    bool needsThis_ = false;
    std::vector<const clang::VarDecl *> passedVariables_;
    std::vector<IifeParameter> parameters_;
    std::optional<RefusalReason> brokenPrecondition_;
    EditMap edits_;
    unsigned headerLength_ = 0;
    unsigned footerLength_ = 0;
};

} // namespace riktig
