#pragma once

#include "Bindings.h"
#include "EditMap.h"
#include "Protocol.h"
#include "Results.h"
#include "Workspace.h"

#include <optional>
#include <set>
#include <string>

namespace riktig {

struct RenameRequest {
    std::string file; // relative to the workspace root, or absolute
    Position at;
    std::string newName;
};

RefactoringResult rename(const Workspace &workspace, const RenameRequest &request);

// The steps of a rename, exposed so the tests' deliberately naive rename can reuse them without the checks.
class RenamePlanner {
  public:
    RenamePlanner(const Workspace &workspace, RenameRequest request);

    std::optional<RefusalReason> parseAndCollect();
    std::optional<RefusalReason> findSymbolAtCursor();
    std::optional<RefusalReason> expandTargets();
    // With refuseUnsafeTokens, refuses when a reference to the symbol is in a macro or outside the workspace.
    std::optional<RefusalReason> collectEdits(bool refuseUnsafeTokens);

    const EditMap &edits() const { return edits_; }
    const std::string &oldName() const { return oldName_; }
    const std::set<DeclIdentity> &targets() const { return targets_; }
    ParsedWorkspace &parsed() { return parsed_; }
    std::vector<TranslationUnitBindings> &translationUnits() { return translationUnits_; }
    const RenameRequest &request() const { return request_; }

  private:
    const Workspace &workspace_;
    RenameRequest request_;
    ParsedWorkspace parsed_;
    std::vector<TranslationUnitBindings> translationUnits_;
    const clang::NamedDecl *symbol_ = nullptr;
    std::string oldName_;
    std::set<DeclIdentity> targets_;
    std::set<std::string> targetUsrs_;
    std::vector<const clang::NamedDecl *> targetDeclarations_;
    EditMap edits_;
};

} // namespace riktig
