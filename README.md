# RiktigRefactor.Cpp

[![Build and test](https://github.com/RiktigRefactor/riktigrefactor.cpp/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/RiktigRefactor/riktigrefactor.cpp/actions/workflows/build-and-test.yml)

Behavior-preserving refactorings for C++, built on clang LibTooling.

*Riktig* (Swedish/Norwegian) means correct.
When the tool cannot prove a transformation is safe, it refuses with a clear reason instead of guessing.

This is the C++ implementation of the [RiktigRefactor specification](https://github.com/RiktigRefactor/riktigrefactor.spec).
The specification defines the correctness model, the refactorings, the JSON protocol, and the test format.

## What it can do

The tool has these refactorings: Rename, Extract IIFE, and Make method static.

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

### Extract IIFE

Extract IIFE wraps a selected expression, or whole statements of one block, in an immediately-invoked lambda:

```cpp
// Before
auto total = price * qty;

// After
auto total = [](double price, int qty) { return price * qty; }(price, qty);
```

Local variables used in the selection become parameters with the same names.
A variable is passed by value only when it is a scalar that the selection only reads and that the function never aliases (no address taken, no reference bound to it, no lambda capturing it by reference); otherwise it is passed by reference, so writes and object identity are kept.
Members are reached through `[this]`.
An lvalue or xvalue expression returns `decltype(auto)`, so assigning to the result or taking its address works as before.
Selected statements are re-indented one level inside the lambda's braces.

It refuses when it can't prove that wrapping keeps behavior, for example when:

- the selection contains `return`, `break` or `continue` leaving it, `goto`, a label, or `co_await`;
- the expression creates a temporary that the lambda would destroy earlier, or a selected statement declares a variable whose destructor would run before the rest of its block;
- something declared in the selected statements is used after them;
- the expression is a null pointer constant `0`, a function name, or a string literal initializing an array;
- the selection uses `__func__` or `std::source_location::current()`, a macro, or a variable inside `decltype`;
- the selection is in a template, a constant expression, or a lambda that captures the variables it uses.

After extracting, it re-parses the workspace and checks that the result compiles, that every name binds as before (with the outer variables now binding to the parameters), and that the call has the expression's type and value category.

### Make method static

Make method static turns an instance member function into a static one on the same class, with the receiver as its first parameter:

```cpp
// Before
double total(int qty) const { return price * qty; }
double t = order.total(3);
double u = p->total(3);

// After
static double total(const Order &self, int qty) { return self.price * qty; }
double t = Order::total(order, 3);
double u = Order::total(*p, 3);
```

A `const` method takes `const Order &`; a non-`const` one takes `Order &`.
Uses of members and of `this` in the body go through the receiver parameter (`self` by default; `--receiver-name` changes it), including in `[&]` lambdas.
Calls from other members pass `*this`.

It refuses when, for example:

- the method is virtual, a template or a member of one, or a template instantiation calls it;
- the method is used other than by calling it, such as `&Order::total`;
- a non-`const` method is called on a temporary;
- a lambda captures `this` by copy or explicitly;
- the receiver and an argument both may have effects: `recv.m(arg)` evaluates `recv` first, but `Order::m(recv, arg)` evaluates them in either order;
- the receiver parameter's name is already used in the method.

Passing the members the method uses instead of the receiver (`pass: "members"`) is not implemented yet.

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

Preview an Extract IIFE of the text from line 12, column 19 up to (not including) column 30:

```shell
riktig-cpp extract-iife --workspace build/compile_commands.json --file src/foo.cpp --from 12:19 --to 12:30
```

Make the method at line 3, column 12 static, naming the receiver `order`:

```shell
riktig-cpp make-method-static --workspace build/compile_commands.json --file src/order.h --at 3:12 --receiver-name order
```

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
