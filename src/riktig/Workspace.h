#pragma once

#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/Error.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace riktig {

// File contents that replace what is on disk, keyed by absolute path.
using FileOverlay = std::map<std::string, std::string>;

class Workspace {
  public:
    // Accepts a compile_commands.json file or the directory that holds it.
    static llvm::Expected<Workspace> load(llvm::StringRef compileCommandsPath);

    const std::string &root() const { return root_; }
    const std::vector<std::string> &translationUnits() const { return translationUnits_; }
    const clang::tooling::CompilationDatabase &compilationDatabase() const { return *database_; }

    bool contains(llvm::StringRef absolutePath) const;
    // Paths inside the workspace are relative to the root with `/` separators; others stay absolute.
    std::string displayPath(llvm::StringRef absolutePath) const;
    std::string absolutePath(llvm::StringRef pathRelativeToRootOrAbsolute) const;
    llvm::Expected<std::string> read(llvm::StringRef absolutePath, const FileOverlay &overlay = {}) const;

  private:
    std::string root_;
    std::shared_ptr<clang::tooling::CompilationDatabase> database_;
    std::vector<std::string> translationUnits_;
};

struct ParsedWorkspace {
    std::vector<std::unique_ptr<clang::ASTUnit>> units;
    std::vector<std::string> errors;
    bool parsedEveryTranslationUnit = true;
};

ParsedWorkspace parse(const Workspace &workspace, const FileOverlay &overlay);

std::string normalizedAbsolutePath(llvm::StringRef path, llvm::StringRef baseDirectory);

} // namespace riktig
