#pragma once

#include "Workspace.h"

#include "clang/AST/Decl.h"
#include "clang/Frontend/ASTUnit.h"

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace riktig {

struct FileOffset {
    std::string file; // absolute, normalized
    unsigned offset = 0;

    auto operator<=>(const FileOffset &) const = default;
};

// A declaration's identity is the location of its canonical declaration's name, so it survives a re-parse after
// edits that only move text.
// The tag tells apart declarations that share a location (a class template and its class, an implicit constructor
// and its class).
struct DeclIdentity {
    FileOffset location; // empty file for declarations with no source location
    std::string tag;

    auto operator<=>(const DeclIdentity &) const = default;
    std::string describe() const;
};

struct BindingKey {
    FileOffset location;
    std::string role;

    auto operator<=>(const BindingKey &) const = default;
};

using BindingTable = std::map<BindingKey, std::set<DeclIdentity>>;

struct TranslationUnitBindings {
    std::string mainFile;
    clang::ASTUnit *unit = nullptr;
    BindingTable bindings;
    // Declarations declared or referenced at a token written in a workspace file, outside any macro.
    std::map<FileOffset, std::vector<const clang::NamedDecl *>> namedAtToken;
    // Same, for tokens written in a macro definition or argument.
    std::map<FileOffset, std::vector<const clang::NamedDecl *>> namedAtTokenInMacro;
    std::vector<const clang::NamedDecl *> declarations;
    std::set<std::string> workspaceFiles;
    std::map<std::string, std::vector<std::pair<unsigned, unsigned>>> skippedRanges;
    // Macro invocations written in workspace files, including their arguments.
    std::map<std::string, std::vector<std::pair<unsigned, unsigned>>> macroExpansionRanges;
    std::set<std::string> macroNames;
};

DeclIdentity identityOf(const clang::NamedDecl *declaration);
std::string usrOf(const clang::Decl *declaration);
std::optional<FileOffset> fileOffsetOf(const clang::SourceManager &sourceManager, clang::SourceLocation location);

std::vector<TranslationUnitBindings> collectBindings(const Workspace &workspace, ParsedWorkspace &parsed);

} // namespace riktig
