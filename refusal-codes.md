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
| `would-introduce-errors` | After the change, a translation unit would not compile. |
| `binding-would-change` | After the change, a reference would bind to a different declaration, a declaration would override or specialize something else, or a new binding would appear. |
| `macro-involved` | A macro refers to the symbol, the old or new name is a macro, or a name is used in a macro argument or a preprocessor directive. |
| `preprocessor-region` | The old or new name appears in a region that conditional compilation skips. |
| `unsupported-reference` | The old or new name appears where the tool does not know what it refers to, such as a dependent name in a template that is never instantiated. |
| `reference-outside-workspace` | Code outside the workspace refers to the symbol by name, for example a standard algorithm calling a user `swap` through argument-dependent lookup. |
| `internal-verification-failed` | The tool's own check of its result failed. This is a bug in the tool; please report it. |
