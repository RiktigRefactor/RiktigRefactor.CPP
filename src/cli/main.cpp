#include "ExtractIife.h"
#include "Rename.h"
#include "Results.h"
#include "Workspace.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace riktig;

namespace {

constexpr int ExitSuccess = 0;
constexpr int ExitRefused = 1;
constexpr int ExitError = 2;

const char *usage = R"(Usage:
  riktig-cpp rename --workspace <compile_commands.json> --file <file> --at <line>:<column> --new-name <name> [--apply] [--json]
  riktig-cpp extract-iife --workspace <compile_commands.json> --file <file> --from <line>:<column> --to <line>:<column> [--apply] [--json]

The workspace is a compile_commands.json file, or the directory holding it.
Positions are 1-based, and columns count Unicode scalar values; --to is exclusive.
By default the tool prints a unified diff; --apply writes the changes, and --json prints the protocol result.
Exit codes: 0 success, 1 refused, 2 error.
)";

std::optional<Position> parsePosition(llvm::StringRef text) {
    auto [line, column] = text.split(':');
    Position position;
    if (line.getAsInteger(10, position.line) || column.getAsInteger(10, position.column)) {
        return std::nullopt;
    }
    return position;
}

std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

// One hunk per file, spanning the first to the last changed line.
void printDiff(const std::string &displayPath, const std::string &before, const std::string &after) {
    auto beforeLines = splitLines(before);
    auto afterLines = splitLines(after);
    size_t prefix = 0;
    while (prefix < beforeLines.size() && prefix < afterLines.size() && beforeLines[prefix] == afterLines[prefix]) {
        ++prefix;
    }
    size_t suffix = 0;
    while (suffix < beforeLines.size() - prefix && suffix < afterLines.size() - prefix &&
           beforeLines[beforeLines.size() - 1 - suffix] == afterLines[afterLines.size() - 1 - suffix]) {
        ++suffix;
    }
    auto &out = llvm::outs();
    out << "--- a/" << displayPath << "\n+++ b/" << displayPath << "\n";
    size_t removed = beforeLines.size() - prefix - suffix;
    size_t added = afterLines.size() - prefix - suffix;
    out << "@@ -" << prefix + 1 << "," << removed << " +" << prefix + 1 << "," << added << " @@\n";
    for (size_t index = prefix; index < prefix + removed; ++index) {
        out << "-" << beforeLines[index] << "\n";
    }
    for (size_t index = prefix; index < prefix + added; ++index) {
        out << "+" << afterLines[index] << "\n";
    }
}

int report(const Workspace &workspace, const RefactoringResult &result, bool json, bool apply) {
    if (json) {
        llvm::outs() << llvm::formatv("{0:2}", toJson(result)) << "\n";
    } else if (!result.succeeded) {
        for (const auto &reason : result.reasons) {
            llvm::errs() << "Refused (" << reason.code << "): " << reason.message << "\n";
            if (reason.location) {
                llvm::errs() << "  at " << reason.location->file << ":" << reason.location->start.line << ":"
                             << reason.location->start.column << "\n";
            }
            if (!reason.suggestion.empty()) {
                llvm::errs() << "  " << reason.suggestion << "\n";
            }
        }
    }
    if (!result.succeeded) {
        return ExitRefused;
    }
    std::map<std::string, std::vector<TextEdit>> editsByFile;
    for (const auto &edit : result.edits) {
        editsByFile[edit.file].push_back(edit);
    }
    for (const auto &[file, edits] : editsByFile) {
        auto path = workspace.absolutePath(file);
        auto before = workspace.read(path);
        if (!before) {
            llvm::errs() << llvm::toString(before.takeError()) << "\n";
            return ExitError;
        }
        EditMap map;
        for (const auto &edit : edits) {
            map.add(path, Replacement{edit.startOffset, edit.endOffset - edit.startOffset, edit.newText});
        }
        auto after = map.apply(path, *before);
        if (apply) {
            std::error_code error;
            llvm::raw_fd_ostream out(path, error);
            if (error) {
                llvm::errs() << "Cannot write " << path << ": " << error.message() << "\n";
                return ExitError;
            }
            out << after;
        } else if (!json) {
            printDiff(file, *before, after);
        }
    }
    return ExitSuccess;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        llvm::errs() << usage;
        return ExitError;
    }
    std::string command = argv[1];
    std::map<std::string, std::string> options;
    bool apply = false;
    bool json = false;
    for (int index = 2; index < argc; ++index) {
        std::string argument = argv[index];
        if (argument == "--apply") {
            apply = true;
        } else if (argument == "--json") {
            json = true;
        } else if (argument.starts_with("--") && index + 1 < argc) {
            options[argument.substr(2)] = argv[++index];
        } else {
            llvm::errs() << "Unexpected argument " << argument << "\n" << usage;
            return ExitError;
        }
    }
    auto workspace = Workspace::load(options["workspace"]);
    if (!workspace) {
        llvm::errs() << llvm::toString(workspace.takeError()) << "\n";
        return ExitError;
    }
    auto file = workspace->absolutePath(options["file"]);
    if (!llvm::sys::fs::exists(file)) {
        llvm::errs() << "No such file: " << options["file"] << "\n";
        return ExitError;
    }
    if (command == "rename") {
        auto at = parsePosition(options["at"]);
        if (!at || options["new-name"].empty()) {
            llvm::errs() << usage;
            return ExitError;
        }
        return report(*workspace, rename(*workspace, RenameRequest{file, *at, options["new-name"]}), json, apply);
    }
    if (command == "extract-iife") {
        auto from = parsePosition(options["from"]);
        auto to = parsePosition(options["to"]);
        if (!from || !to) {
            llvm::errs() << usage;
            return ExitError;
        }
        return report(*workspace, extractIife(*workspace, ExtractIifeRequest{file, *from, *to}), json, apply);
    }
    llvm::errs() << "Unknown command " << command << "\n" << usage;
    return ExitError;
}
