#include "Naive.h"

namespace riktig {

RefactoringResult naiveMoveToInstanceMethod(const Workspace &workspace, const MoveToInstanceMethodRequest &request) {
    MoveToInstanceMethodPlanner planner(workspace, request);
    for (auto step : {&MoveToInstanceMethodPlanner::parseAndCollect,
                      &MoveToInstanceMethodPlanner::findMethodAndReceiver, &MoveToInstanceMethodPlanner::writeEdits}) {
        if (auto refusal = (planner.*step)()) {
            return RefactoringResult::refused(workspace.root(), *refusal);
        }
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.plan().edits()));
}

} // namespace riktig
