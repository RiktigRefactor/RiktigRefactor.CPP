#include "Naive.h"

namespace riktig {

RefactoringResult naiveExtractObjectFromArguments(const Workspace &workspace,
                                                  const ExtractObjectFromArgumentsRequest &request) {
    ExtractObjectPlanner planner(workspace, request);
    for (auto step : {&ExtractObjectPlanner::parseAndCollect, &ExtractObjectPlanner::findFunctionAndParameters,
                      &ExtractObjectPlanner::writeEdits}) {
        if (auto refusal = (planner.*step)()) {
            return RefactoringResult::refused(workspace.root(), *refusal);
        }
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.plan().edits()));
}

} // namespace riktig
