#include "Results.h"

#include "SourceText.h"

#include "llvm/Support/raw_ostream.h"

namespace riktig {

RefusalReason makeRefusal(const char *code, std::string message, std::optional<FileRange> location,
                          std::string suggestion) {
    return RefusalReason{code, std::move(message), std::move(location), std::move(suggestion)};
}

std::optional<FileRange> rangeOf(const Workspace &workspace, const FileOffset &location, unsigned length,
                                 const FileOverlay &overlay) {
    auto text = workspace.read(location.file, overlay);
    if (!text) {
        llvm::consumeError(text.takeError());
        return std::nullopt;
    }
    return FileRange{workspace.displayPath(location.file), positionOfOffset(*text, location.offset),
                     positionOfOffset(*text, location.offset + length)};
}

FileOverlay applyEdits(const Workspace &workspace, const EditMap &edits) {
    FileOverlay overlay;
    for (const auto &[file, replacements] : edits.replacements()) {
        auto original = workspace.read(file);
        if (!original) {
            llvm::consumeError(original.takeError());
            continue;
        }
        overlay[file] = edits.apply(file, *original);
    }
    return overlay;
}

std::vector<TextEdit> protocolEdits(const Workspace &workspace, const EditMap &edits) {
    std::vector<TextEdit> result;
    for (const auto &[file, replacements] : edits.replacements()) {
        auto original = workspace.read(file);
        if (!original) {
            llvm::consumeError(original.takeError());
            continue;
        }
        for (const auto &replacement : replacements) {
            TextEdit edit;
            edit.file = workspace.displayPath(file);
            edit.startOffset = replacement.offset;
            edit.endOffset = replacement.offset + replacement.length;
            edit.newText = replacement.newText;
            edit.start = positionOfOffset(*original, edit.startOffset);
            edit.end = positionOfOffset(*original, edit.endOffset);
            result.push_back(std::move(edit));
        }
    }
    return result;
}

} // namespace riktig
