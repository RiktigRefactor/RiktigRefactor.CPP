#pragma once

#include "ConvertToInstanceMethod.h"
#include "ExtractIife.h"
#include "ExtractObjectFromArguments.h"
#include "MakeMethodStatic.h"
#include "MoveStaticMember.h"
#include "Protocol.h"
#include "Rename.h"
#include "Workspace.h"

namespace riktig {

// Deliberately naive implementations: the same edits as the real refactorings, with no preconditions and no
// verification. Edge-case tests must fail under them, which proves their assertions can detect a behavior change.
RefactoringResult naiveRename(const Workspace &workspace, const RenameRequest &request);
RefactoringResult naiveExtractIife(const Workspace &workspace, const ExtractIifeRequest &request);
RefactoringResult naiveMakeMethodStatic(const Workspace &workspace, const MakeMethodStaticRequest &request);
RefactoringResult naiveMoveStaticMember(const Workspace &workspace, const MoveStaticMemberRequest &request);
RefactoringResult naiveExtractObjectFromArguments(const Workspace &workspace,
                                                  const ExtractObjectFromArgumentsRequest &request);
RefactoringResult naiveConvertToInstanceMethod(const Workspace &workspace,
                                               const ConvertToInstanceMethodRequest &request);

} // namespace riktig
