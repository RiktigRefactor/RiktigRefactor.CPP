#include "Naive.h"

namespace riktig {

RefactoringResult naiveExtractIife(const Workspace &workspace, const ExtractIifeRequest &request) {
    IifePlanner planner(workspace, request);
    for (auto step : {&IifePlanner::parseAndCollect, &IifePlanner::analyzeSelection, &IifePlanner::writeEdits}) {
        if (auto refusal = (planner.*step)()) {
            return RefactoringResult::refused(workspace.root(), *refusal);
        }
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.edits()));
}

} // namespace riktig
