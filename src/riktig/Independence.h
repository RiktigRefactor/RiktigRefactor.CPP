#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"

#include <set>

namespace riktig {

// Decides whether two expressions are independent in the sense of the specification's principles.md: evaluating
// them in either order gives the same behavior. When it can't prove that, it says they are dependent.
class IndependenceAnalysis {
  public:
    // `enclosingFunction` holds both expressions; its locals that nothing aliases can only change by a direct write.
    IndependenceAnalysis(clang::ASTContext &context, const clang::FunctionDecl *enclosingFunction);

    bool areIndependent(const clang::Expr *first, const clang::Expr *second) const;

  private:
    struct Facts {
        // No observable effect, and reads nothing but literals and locals that nothing aliases.
        bool readsOnlyUnaliasedLocals = false;
        std::set<const clang::VarDecl *> readLocals;
        std::set<const clang::VarDecl *> writtenLocals;
    };
    Facts factsOf(const clang::Expr *expression) const;
    bool isEffectFreeValue(const clang::Expr *expression, std::set<const clang::VarDecl *> &reads) const;
    bool isEffectFreeGlvalue(const clang::Expr *expression, std::set<const clang::VarDecl *> &reads) const;
    bool isUnaliasedLocal(const clang::VarDecl *variable) const;

    clang::ASTContext &context_;
    const clang::FunctionDecl *enclosingFunction_;
    std::set<const clang::VarDecl *> aliased_;
};

} // namespace riktig
