# Refusal codes

The C++ tool defines its own refusal codes; the [specification](https://github.com/RiktigRefactor/riktigrefactor.spec) does not list any, except `internal-verification-failed`.
This file is the source of truth for them, and `src/riktig/RefusalCodes.h` defines them in code.
Codes are stable, and messages may change.
A new code is listed in the release notes.

| Code | Meaning |
|---|---|
| `workspace-does-not-compile` | A translation unit in `compile_commands.json` has a compile error, so its bindings can't be trusted. |
| `invalid-location` | The file can't be read, or the position is not in it. |
| `no-symbol` | There is no renameable symbol at the cursor. |
| `ambiguous-symbol` | The identifier at the cursor refers to more than one symbol, such as an overload set in a `using` declaration. |
| `not-renameable` | The symbol can't be renamed: it is declared outside the workspace, overrides a member declared there, is `main`, has C language linkage, is not named by an identifier (operators, conversion functions), or is related to a declaration with another name. |
| `invalid-identifier` | The new name is not an identifier, is a keyword or alternative token, is reserved (contains `__` or starts with `_` and an uppercase letter), or is the old name. |
| `conflicting-declaration` | The renamed symbol would become the same entity as an existing one, for example a function with external linkage defined in another translation unit. |
| `would-introduce-errors` | After the rename, a translation unit would not compile. |
| `binding-would-change` | After the change, a reference would bind to a different declaration, a declaration would override or specialize something else, or a new binding would appear. |
| `macro-involved` | Rename: a macro refers to the symbol, the old or new name is a macro, or a name is used in a macro argument or a preprocessor directive. Extract IIFE: the selection uses a macro or contains a directive. |
| `preprocessor-region` | The old or new name appears in a region that conditional compilation skips, or the Extract IIFE selection contains one. |
| `unsupported-reference` | The old or new name appears where the tool does not know what it refers to, such as a dependent name in a template that is never instantiated. |
| `reference-outside-workspace` | Code outside the workspace refers to the symbol by name, for example a standard algorithm calling a user `swap` through argument-dependent lookup. |
| `invalid-selection` | Extract IIFE: the selection is not exactly one expression or whole statements of one block, or is a braced initializer list. |
| `unsupported-context` | Extract IIFE: the selection is not in a function body, is in a template or generic lambda, in a constant expression (such as a `case` label or `if constexpr` condition), in the initializer of a static or `constexpr` variable, or has a literal spanning lines that re-indenting would change. |
| `non-local-control-flow` | Extract IIFE: `return`, `break`, `continue`, `goto`, a label, or a `case` label would cross the boundary of the lambda. |
| `contains-coroutine-keyword` | Extract IIFE: the selection contains `co_await`, `co_yield` or `co_return`, or is in a coroutine. |
| `type-not-nameable` | Extract IIFE: the type of a variable that would become a parameter can't be written out (a lambda, unnamed or local type), a structured binding crosses the boundary, or the expression is a cv-qualified class prvalue. |
| `target-typed-expression` | Extract IIFE: the expression's meaning depends on how it is used, such as a null pointer constant `0`, a string literal initializing an array, or a function name or overload set. |
| `temporary-lifetime` | Extract IIFE: the expression creates a temporary that would be destroyed when the lambda returns instead of at the end of the full-expression, and that is observable (a non-trivial destructor, or a result that could refer to it). |
| `destructor-timing` | Extract IIFE: a variable declared in the selected statements would be destroyed when the lambda returns, before the rest of its block runs. |
| `declaration-escapes` | Extract IIFE: something declared in the selected statements is used after them. |
| `captured-variable` | Extract IIFE: a variable crossing the boundary is captured by a lambda in the selection, or by the lambda enclosing the selection. |
| `function-name-observed` | Extract IIFE: the selection uses `__func__`, `__PRETTY_FUNCTION__`, or `std::source_location::current()`, whose value would name the lambda. |
| `unevaluated-operand` | Extract IIFE: the selection is an unevaluated operand, or uses a variable crossing the boundary in one (such as `decltype`), where the parameter's reference type would show. |
| `bit-field` | Extract IIFE: the expression is a bit-field, which a lambda can't return by reference. |
| `internal-verification-failed` | The tool's own check of its result failed. This is a bug in the tool; please report it. |
