#pragma once

#include "EditMap.h"
#include "Protocol.h"
#include "Workspace.h"

#include <optional>
#include <string>
#include <vector>

namespace riktig {

RefusalReason makeRefusal(const char *code, std::string message, std::optional<FileRange> location = std::nullopt,
                          std::string suggestion = {});
std::optional<FileRange> rangeOf(const Workspace &workspace, const FileOffset &location, unsigned length = 0,
                                 const FileOverlay &overlay = {});
FileOverlay applyEdits(const Workspace &workspace, const EditMap &edits);
std::vector<TextEdit> protocolEdits(const Workspace &workspace, const EditMap &edits);

} // namespace riktig
