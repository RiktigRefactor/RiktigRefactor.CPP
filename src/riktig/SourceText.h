#pragma once

#include "Protocol.h"

#include "llvm/ADT/StringRef.h"

#include <optional>

namespace riktig {

// Converts between byte offsets and protocol positions, whose columns count Unicode scalar values.
Position positionOfOffset(llvm::StringRef text, unsigned offset);
std::optional<unsigned> offsetOfPosition(llvm::StringRef text, Position position);

} // namespace riktig
