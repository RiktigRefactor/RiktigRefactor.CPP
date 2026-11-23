#pragma once

#include "Protocol.h"
#include "Rename.h"
#include "Workspace.h"

namespace riktig {

// Deliberately naive implementations: the same edits as the real refactorings, with no preconditions and no
// verification. Edge-case tests must fail under them, which proves their assertions can detect a behavior change.
RefactoringResult naiveRename(const Workspace &workspace, const RenameRequest &request);

} // namespace riktig
