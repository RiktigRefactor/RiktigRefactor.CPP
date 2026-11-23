#include "Naive.h"

#include "Results.h"

namespace riktig {

RefactoringResult naiveRename(const Workspace &workspace, const RenameRequest &request) {
    RenamePlanner planner(workspace, request);
    for (auto step : {&RenamePlanner::parseAndCollect, &RenamePlanner::findSymbolAtCursor}) {
        if (auto refusal = (planner.*step)()) {
            return RefactoringResult::refused(workspace.root(), *refusal);
        }
    }
    planner.expandTargets();
    planner.collectEdits(/*refuseUnsafeTokens=*/false);
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.edits()));
}

} // namespace riktig
