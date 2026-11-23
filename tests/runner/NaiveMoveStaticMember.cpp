#include "Naive.h"

namespace riktig {

RefactoringResult naiveMoveStaticMember(const Workspace &workspace, const MoveStaticMemberRequest &request) {
    MoveStaticMemberPlanner planner(workspace, request);
    for (auto step : {&MoveStaticMemberPlanner::parseAndCollect, &MoveStaticMemberPlanner::findMemberAndTarget,
                      &MoveStaticMemberPlanner::writeEdits}) {
        if (auto refusal = (planner.*step)()) {
            return RefactoringResult::refused(workspace.root(), *refusal);
        }
    }
    return RefactoringResult::success(workspace.root(), protocolEdits(workspace, planner.plan().edits()));
}

} // namespace riktig
