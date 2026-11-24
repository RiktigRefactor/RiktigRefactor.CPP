#pragma once

#include "Bindings.h"
#include "EditMap.h"
#include "Protocol.h"
#include "Verification.h"
#include "Workspace.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Support shared by the refactorings that change a function and every call to it: Make method static, Move static
// member, Convert static method to instance method, and Extract object from arguments.
namespace riktig {

struct LoadedWorkspace {
    ParsedWorkspace parsed;
    std::vector<TranslationUnitBindings> units;
};

// Parses every translation unit and records its bindings; refuses when the workspace does not compile.
std::optional<RefusalReason> loadWorkspace(const Workspace &workspace, LoadedWorkspace &loaded);

// The declaration named by the identifier at the cursor, as a declaration or a reference.
std::optional<RefusalReason> findDeclarationAtCursor(const Workspace &workspace, const LoadedWorkspace &loaded,
                                                     const std::string &file, Position at,
                                                     const clang::NamedDecl *&found);

struct OffsetRange {
    std::string file;
    unsigned begin = 0;
    unsigned end = 0;
};

// The file range of source written outside macros, from the start of `range`'s first token to the end of its last.
std::optional<OffsetRange> fileRangeOf(const clang::ASTContext &context, clang::SourceRange range);
std::optional<FileOffset> fileOffsetOfLocation(const clang::ASTContext &context, clang::SourceLocation location);
// The offset just after the token at `location`.
std::optional<unsigned> endOfTokenAt(const clang::ASTContext &context, clang::SourceLocation location);
// The next token after the one at `location`, if it is written in a file.
std::optional<OffsetRange> nextTokenAfter(const clang::ASTContext &context, clang::SourceLocation location);

struct Reference {
    TranslationUnitBindings *unit = nullptr;
    clang::ASTContext *context = nullptr;
    // A DeclRefExpr or MemberExpr naming the target.
    const clang::Expr *expression = nullptr;
    // The call whose callee is the reference, if any.
    const clang::CallExpr *call = nullptr;
    // The innermost semantic context of the reference, and the function whose body holds it, if any.
    const clang::DeclContext *semanticContext = nullptr;
    const clang::FunctionDecl *enclosingFunction = nullptr;
    bool inTemplateInstantiation = false;
    bool inWorkspace = true;
    bool inMacro = false;
};

// Every reference to the function or variable in every translation unit, including in template instantiations.
std::vector<Reference> findReferences(const Workspace &workspace, LoadedWorkspace &loaded,
                                      const clang::ValueDecl *target);

// Every declaration of the function or variable in every translation unit, once per location.
std::vector<const clang::ValueDecl *> declarationsOf(const LoadedWorkspace &loaded, const clang::ValueDecl *target);

// The written arguments of a call, without default arguments.
std::vector<const clang::Expr *> writtenArguments(const clang::CallExpr *call);

struct QualifierComponent {
    const clang::NamedDecl *declaration = nullptr; // a namespace or class
    std::string name;
};

// How to name `target` (a class or namespace) from code in `from`: its enclosing namespaces and classes, without the
// namespaces that already enclose `from`. Nothing when a component can't be named, such as an unnamed class.
std::optional<std::vector<QualifierComponent>> qualifierFrom(const clang::DeclContext *target,
                                                             const clang::DeclContext *from);
std::string spell(const std::vector<QualifierComponent> &qualifier);

// Edits, deduplicated across translation units, with what the re-binding check needs to know about inserted text.
class EditPlan {
  public:
    explicit EditPlan(const Workspace &workspace) : workspace_(workspace) {}

    const std::string &text(const std::string &file);
    // Returns false when the replacement overlaps another.
    bool replace(const std::string &file, unsigned offset, unsigned length, std::string newText);
    bool insert(const std::string &file, unsigned offset, std::string newText) {
        return replace(file, offset, 0, std::move(newText));
    }
    // Records that [from, from + length) of `file` is copied into the new text of the replacement at
    // `replacementOffset` of `destinationFile`, starting at `positionInNewText`, with `innerEdits` (offsets relative
    // to `from`) applied to the copy, so its bindings are expected there.
    void relocate(const std::string &file, unsigned from, unsigned length, const std::string &destinationFile,
                  unsigned replacementOffset, unsigned replacementLength, unsigned positionInNewText,
                  std::vector<Replacement> innerEdits = {});
    // Where an offset relative to `from` of relocated text is in the copy, after its inner edits.
    static unsigned offsetInCopy(const std::vector<Replacement> &innerEdits, unsigned relativeOffset);
    // Records that inserted text at `positionInNewText` must bind with `role` to the declaration found by
    // `identity` (given in the original files' coordinates, or after the edits when `identityIsAfter`).
    void expectBinding(const std::string &file, unsigned replacementOffset, unsigned replacementLength,
                       unsigned positionInNewText, std::string role, DeclIdentity identity,
                       bool identityIsAfter = false);

    // Expects each component of a qualifier written at `positionInNewText` to bind to its namespace or class.
    void expectQualifier(const std::string &file, unsigned replacementOffset, unsigned replacementLength,
                         unsigned positionInNewText, const std::vector<QualifierComponent> &qualifier);

    const EditMap &edits() const { return edits_; }
    // Where a position in a replacement's new text is after the edits.
    unsigned afterOffset(const std::string &file, unsigned replacementOffset, unsigned replacementLength,
                         unsigned positionInNewText) const;

    BindingExpectations expectations() const;
    // Expected bindings in inserted text that the re-parse did not find; call after compareBindings succeeded.
    std::optional<std::string> missingExpectedBinding(const std::vector<TranslationUnitBindings> &after) const;

  private:
    struct Relocation {
        std::string file;
        unsigned from;
        unsigned length;
        std::string destinationFile;
        unsigned replacementOffset;
        unsigned replacementLength;
        unsigned positionInNewText;
        std::vector<Replacement> innerEdits;
    };
    struct Expectation {
        std::string file;
        unsigned replacementOffset;
        unsigned replacementLength;
        unsigned positionInNewText;
        std::string role;
        DeclIdentity identity;
        bool identityIsAfter;
    };
    std::optional<FileOffset> relocated(const FileOffset &before) const;
    bool isInInsertedText(const FileOffset &after) const;

    const Workspace &workspace_;
    std::map<std::string, std::string> texts_;
    EditMap edits_;
    std::vector<Relocation> relocations_;
    std::vector<Expectation> expectations_;
};

// Re-parses the workspace with the edits and checks that it compiles and that every binding is as expected.
// `intended` says which bindings intentionally change (its substitute, isNew and isDropped are used).
std::optional<RefusalReason> verifyEditPlan(const Workspace &workspace, const LoadedWorkspace &before,
                                            const EditPlan &plan, const BindingExpectations &intended,
                                            const std::string &refactoringName);

// The source text of a range.
std::string textOf(EditPlan &plan, const OffsetRange &range);

} // namespace riktig
