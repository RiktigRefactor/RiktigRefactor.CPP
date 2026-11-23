# RiktigRefactor.Cpp

[![Build and test](https://github.com/RiktigRefactor/riktigrefactor.cpp/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/RiktigRefactor/riktigrefactor.cpp/actions/workflows/build-and-test.yml)

Behavior-preserving refactorings for C++, built on clang LibTooling.

*Riktig* (Swedish/Norwegian) means correct.
When the tool cannot prove a transformation is safe, it refuses with a clear reason instead of guessing.

This is the C++ implementation of the [RiktigRefactor specification](https://github.com/RiktigRefactor/riktigrefactor.spec).
The specification defines the correctness model, the refactorings, the JSON protocol, and the test format.

## What it can do

### Rename

Rename renames a symbol and every reference to it across every translation unit in `compile_commands.json`, including the headers they include.
It renames a class with its constructors, destructor, specializations and deduction guides, a function template with its explicit specializations, a virtual member with every override in the workspace, and a declaration with its redeclarations in other translation units.

Before renaming, the tool parses every translation unit and records what every name binds to, including in template instantiations.
After renaming, it parses them again and checks that every reference binds to the same declaration as before, that overrides and specializations are unchanged, and that the renamed entity does not become the same entity as an existing one in another translation unit.
It refuses (exit code 1) when, for example:

- a name would be captured by another declaration, or a call would choose another overload, including through argument-dependent lookup in a template instantiation;
- the change would not compile;
- the symbol is declared outside the workspace, overrides a member declared there, is `main`, or has C language linkage;
- a macro mentions the symbol, or the old or new name is a macro, or appears in a skipped `#if` region;
- the old or new name appears where the tool can't tell what it refers to, such as a dependent name in a template that is never instantiated.

Each refusal has a code, listed in [refusal-codes.md](refusal-codes.md).

See [limitations](https://github.com/RiktigRefactor/riktigrefactor.spec/blob/main/limitations.md) for what is out of scope, such as updating comments and strings.
Names captured as strings change with the rename, by design: `__func__`, `typeid(T).name()`, and `std::source_location::function_name()` report the new name.

## Command line

```shell
riktig-cpp rename --workspace build/compile_commands.json --file src/foo.cpp --at 12:5 --new-name count
```

Positions are 1-based, and columns count Unicode scalar values.
`--workspace` accepts `compile_commands.json` or the directory holding it; the tool needs it to resolve includes and macros, and refuses to guess without it.
CMake writes it with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

- `--apply` writes the changes to disk.
- `--json` prints the result using the shared protocol, without the diff.

The exit code is 0 on success, 1 when the refactoring is refused, and 2 on an error such as an unknown file.

There are no release binaries yet; build from a clone and run `build/riktig-cpp`.

## Build and test

The tool is pinned to clang 18.
On Ubuntu 24.04:

```shell
sudo apt-get install clang-18 clang-format-18 libclang-18-dev llvm-18-dev cmake ninja-build
./build-and-test
```

`build-and-test` checks formatting, builds, and runs every test case; `build-and-test.cmd` does the same on Windows through Git for Windows' Bash, but the build is only tested on Linux.
Run `./tidy-code` to format the code.
See [UPDATING_DEPENDENCIES.md](UPDATING_DEPENDENCIES.md) for moving to a newer clang.

Test cases live in `tests/cases`, in the [format defined by the specification](https://github.com/RiktigRefactor/riktigrefactor.spec/blob/main/test-format.md).
Every input and expected program is compiled and run, and edge cases must fail under a deliberately naive implementation.

### Citing the C++ standard

Every test case lists the sections of the C++ standard it covers in the `spec` field of its `case.json`, by stable name, written as `C++ [<name>]`, for example `C++ [over.match]`.
Stable names follow the C++20 standard (ISO/IEC 14882:2020) as listed at [eel.is/c++draft](https://eel.is/c++draft/).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the commit message convention, and [AGENTS.md](AGENTS.md) for how the repo is organized.
