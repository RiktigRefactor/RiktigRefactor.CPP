#include "SourceText.h"

namespace riktig {

static bool isUtf8ContinuationByte(char byte) { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; }

Position positionOfOffset(llvm::StringRef text, unsigned offset) {
    Position position;
    for (unsigned index = 0; index < offset && index < text.size(); ++index) {
        if (text[index] == '\n') {
            ++position.line;
            position.column = 1;
        } else if (!isUtf8ContinuationByte(text[index])) {
            ++position.column;
        }
    }
    return position;
}

std::optional<unsigned> offsetOfPosition(llvm::StringRef text, Position position) {
    if (position.line == 0 || position.column == 0) {
        return std::nullopt;
    }
    unsigned index = 0;
    for (unsigned line = 1; line < position.line; ++line) {
        auto newline = text.find('\n', index);
        if (newline == llvm::StringRef::npos) {
            return std::nullopt;
        }
        index = newline + 1;
    }
    for (unsigned column = 1; column < position.column; ++column) {
        if (index >= text.size() || text[index] == '\n') {
            return std::nullopt;
        }
        ++index;
        while (index < text.size() && isUtf8ContinuationByte(text[index])) {
            ++index;
        }
    }
    return index;
}

} // namespace riktig
