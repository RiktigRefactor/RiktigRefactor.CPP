#pragma once

#include "Bindings.h"
#include "Protocol.h"
#include "Workspace.h"

#include "llvm/ADT/StringRef.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace riktig {

bool isValidIdentifier(llvm::StringRef name);
// Contains `__` or starts with `_` and an uppercase letter.
bool isReservedIdentifier(llvm::StringRef name);

// Refuses a new name that is not an identifier, is a keyword, is reserved, or is the old name.
std::optional<RefusalReason> validateNewName(const std::string &oldName, const std::string &newName);

// Refuses when one of the names appears in a workspace file where the tool does not know what it refers to: in a
// region that conditional compilation skips, in a directive, in a macro argument, or where no binding was recorded.
std::optional<RefusalReason> checkEveryMentionIsUnderstood(const Workspace &workspace,
                                                           const std::vector<TranslationUnitBindings> &units,
                                                           const std::set<std::string> &names,
                                                           const FileOverlay &overlay);

} // namespace riktig
