#include "Protocol.h"

namespace riktig {

RefactoringResult RefactoringResult::refused(std::string workspaceRoot, RefusalReason reason) {
    RefactoringResult result;
    result.workspaceRoot = std::move(workspaceRoot);
    result.reasons.push_back(std::move(reason));
    return result;
}

RefactoringResult RefactoringResult::success(std::string workspaceRoot, std::vector<TextEdit> edits) {
    RefactoringResult result;
    result.succeeded = true;
    result.workspaceRoot = std::move(workspaceRoot);
    result.edits = std::move(edits);
    return result;
}

static llvm::json::Value toJson(const Position &position) {
    return llvm::json::Object{{"line", position.line}, {"column", position.column}};
}

static llvm::json::Value toJson(const FileRange &range) {
    return llvm::json::Object{{"file", range.file}, {"start", toJson(range.start)}, {"end", toJson(range.end)}};
}

llvm::json::Value toJson(const RefactoringResult &result) {
    llvm::json::Object object{{"status", result.succeeded ? "success" : "refused"},
                              {"workspaceRoot", result.workspaceRoot}};
    if (result.succeeded) {
        llvm::json::Array edits;
        for (const auto &edit : result.edits) {
            edits.push_back(llvm::json::Object{{"file", edit.file},
                                               {"start", toJson(edit.start)},
                                               {"end", toJson(edit.end)},
                                               {"newText", edit.newText}});
        }
        object["edits"] = std::move(edits);
        llvm::json::Array warnings;
        for (const auto &warning : result.warnings) {
            llvm::json::Object entry{{"code", warning.code}, {"message", warning.message}};
            if (warning.location) {
                entry["location"] = toJson(*warning.location);
            }
            warnings.push_back(std::move(entry));
        }
        object["warnings"] = std::move(warnings);
    } else {
        llvm::json::Array reasons;
        for (const auto &reason : result.reasons) {
            llvm::json::Object entry{{"code", reason.code}, {"message", reason.message}};
            if (reason.location) {
                entry["location"] = toJson(*reason.location);
            }
            if (!reason.suggestion.empty()) {
                entry["suggestion"] = reason.suggestion;
            }
            reasons.push_back(std::move(entry));
        }
        object["reasons"] = std::move(reasons);
    }
    return object;
}

} // namespace riktig
