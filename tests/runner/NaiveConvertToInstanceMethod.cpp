#include "Naive.h"

namespace riktig {

RefactoringResult naiveConvertToInstanceMethod(const Workspace &workspace, const ConvertToInstanceMethodRequest &request) {
    ConvertToInstanceMethodPlanner planner(workspace, request);
    for (auto step : {&ConvertToInstanceMethodPlanner::parseAndCollect,
                      &ConvertToInstanceMethodPlanner::findMethodAndReceiver, &ConvertToInstanceMethodPlanner::writeEdits}) {
        if (auto refusal = (planner.*step)()) {
            return RefactoringResult::refused(workspace.root(), *refusal);
        }
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.plan().edits()));
}

} // namespace riktig
