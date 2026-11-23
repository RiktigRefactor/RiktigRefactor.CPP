#pragma once

#include "llvm/Support/JSON.h"

#include <optional>
#include <string>
#include <vector>

namespace riktig {

// 1-based; columns count Unicode scalar values.
struct Position {
    unsigned line = 1;
    unsigned column = 1;
};

struct FileRange {
    std::string file;
    Position start;
    Position end;
};

struct TextEdit {
    std::string file;
    unsigned startOffset = 0;
    unsigned endOffset = 0;
    std::string newText;
    Position start;
    Position end;
};

struct RefusalReason {
    std::string code;
    std::string message;
    std::optional<FileRange> location;
    std::string suggestion;
};

struct Warning {
    std::string code;
    std::string message;
    std::optional<FileRange> location;
};

struct RefactoringResult {
    bool succeeded = false;
    std::string workspaceRoot;
    std::vector<TextEdit> edits;
    std::vector<Warning> warnings;
    std::vector<RefusalReason> reasons;

    static RefactoringResult refused(std::string workspaceRoot, RefusalReason reason);
    static RefactoringResult success(std::string workspaceRoot, std::vector<TextEdit> edits);
};

llvm::json::Value toJson(const RefactoringResult &result);

} // namespace riktig
