#include "Naive.h"

namespace riktig {

RefactoringResult naiveMakeMethodStatic(const Workspace &workspace, const MakeMethodStaticRequest &request) {
    MakeMethodStaticPlanner planner(workspace, request);
    for (auto step : {&MakeMethodStaticPlanner::parseAndCollect, &MakeMethodStaticPlanner::findMethod,
                      &MakeMethodStaticPlanner::writeEdits}) {
        if (auto refusal = (planner.*step)()) {
            return RefactoringResult::refused(workspace.root(), *refusal);
        }
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.plan().edits()));
}

} // namespace riktig
