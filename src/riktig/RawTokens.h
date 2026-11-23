#pragma once

#include "llvm/ADT/StringRef.h"

#include <functional>
#include <optional>
#include <string>

namespace riktig {

struct RawIdentifier {
    unsigned offset = 0;
    llvm::StringRef text;
    // The directive's name ("define", "include", ...) when the identifier is on a preprocessor directive line.
    llvm::StringRef directive;
};

// Lexes C++ source without preprocessing, so identifiers in comments and string literals are not reported.
void forEachRawIdentifier(llvm::StringRef source, const std::function<void(const RawIdentifier &)> &visit);

std::optional<std::string> identifierAt(llvm::StringRef source, unsigned offset);

// Start of the identifier containing the cursor, or ending right before it.
std::optional<unsigned> identifierStartAround(llvm::StringRef source, unsigned cursorOffset);

bool isCppKeyword(llvm::StringRef name);

// A string or character literal that spans lines, such as a raw string literal, whose text re-indenting would change.
bool hasLiteralSpanningLines(llvm::StringRef source);

bool hasPreprocessorDirective(llvm::StringRef source);

} // namespace riktig
