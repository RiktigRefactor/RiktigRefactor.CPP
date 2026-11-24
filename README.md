# RiktigRefactor.Cpp

[![Build and test](https://github.com/RiktigRefactor/riktigrefactor.cpp/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/RiktigRefactor/riktigrefactor.cpp/actions/workflows/build-and-test.yml)
![Status: early development](https://img.shields.io/badge/status-early%20development-orange)

Seriously safe behavior-preserving refactorings for C++.
*Riktig* means "correct" in Swedish/Norwegian.
When the tool cannot prove a transformation is safe, it refuses with a clear reason instead of guessing.

This is the C++ implementation of the [RiktigRefactor specification](https://github.com/RiktigRefactor/riktigrefactor.spec).

<!-- toc -->
## Contents

  * [What it can do](#what-it-can-do)
    * [Rename](#rename)
    * [Extract IIFE](#extract-iife)
    * [Make method static](#make-method-static)
    * [Move static member](#move-static-member)
    * [Convert static method to instance method](#convert-static-method-to-instance-method)
    * [Extract object from arguments](#extract-object-from-arguments)
  * [Using it](#using-it)
  * [Building and testing](#building-and-testing)<!-- endToc -->

## What it can do

The tool has these refactorings:

### Rename

Rename renames a symbol and every reference to it across every translation unit in `compile_commands.json`, including the headers they include.

#### Before

<!-- snippet: rename-before -->
<a id='snippet-rename-before'></a>
```cpp
struct Size {
    int /*|*/w;
    int h;
};
```
<sup><a href='/tests/cases/rename/basic/field-with-initializers/input/main.cpp#L1-L6' title='Snippet source file'>snippet source</a> | <a href='#snippet-rename-before' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

#### After

<!-- snippet: rename-after -->
<a id='snippet-rename-after'></a>
```cpp
struct Size {
    int width;
    int h;
};
```
<sup><a href='/tests/cases/rename/basic/field-with-initializers/expected/main.cpp#L1-L6' title='Snippet source file'>snippet source</a> | <a href='#snippet-rename-after' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

See more details in [docs/rename.md](docs/rename.md).

### Extract IIFE

Extract IIFE wraps a selected expression, or whole statements of one block, in an immediately-invoked lambda.

#### Before

<!-- snippet: extract-iife-before -->
<a id='snippet-extract-iife-before'></a>
```cpp
auto total = /*[*/price * qty/*]*/;
```
<sup><a href='/tests/cases/extract-iife/expression/arithmetic/input/main.cpp#L4-L6' title='Snippet source file'>snippet source</a> | <a href='#snippet-extract-iife-before' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

#### After

<!-- snippet: extract-iife-after -->
<a id='snippet-extract-iife-after'></a>
```cpp
auto total = [](double price, int qty) { return price * qty; }(price, qty);
```
<sup><a href='/tests/cases/extract-iife/expression/arithmetic/expected/main.cpp#L4-L6' title='Snippet source file'>snippet source</a> | <a href='#snippet-extract-iife-after' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

See more details in [docs/extract-iife.md](docs/extract-iife.md).

### Make method static

Make method static turns an instance member function into a static one, with the receiver as its first parameter.

#### Before

<!-- snippet: make-method-static-before -->
<a id='snippet-make-method-static-before'></a>
```cpp
double /*|*/total(int qty) const { return price * qty; }
```
<sup><a href='/tests/cases/make-method-static/basic/const-method/input/main.cpp#L3-L5' title='Snippet source file'>snippet source</a> | <a href='#snippet-make-method-static-before' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

#### After

<!-- snippet: make-method-static-after -->
<a id='snippet-make-method-static-after'></a>
```cpp
static double total(const Order &self, int qty) { return self.price * qty; }
```
<sup><a href='/tests/cases/make-method-static/basic/const-method/expected/main.cpp#L3-L5' title='Snippet source file'>snippet source</a> | <a href='#snippet-make-method-static-after' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

See more details in [docs/make-method-static.md](docs/make-method-static.md).

### Move static member

Move static member moves a static member function or `constexpr` static data member to another class, or a namespace-scope function or `constexpr` variable to another namespace, and updates every reference.

#### Before

<!-- snippet: move-static-member-before -->
<a id='snippet-move-static-member-before'></a>
```cpp
static double /*|*/total(const Order &self, int qty) { return self.quantity * qty * rate; }
```
<sup><a href='/tests/cases/move-static-member/basic/static-method-to-class/input/main.cpp#L5-L7' title='Snippet source file'>snippet source</a> | <a href='#snippet-move-static-member-before' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

#### After

<!-- snippet: move-static-member-after -->
<a id='snippet-move-static-member-after'></a>
```cpp
static double total(const Order &self, int qty) { return self.quantity * qty * Order::rate; }
```
<sup><a href='/tests/cases/move-static-member/basic/static-method-to-class/expected/main.cpp#L11-L13' title='Snippet source file'>snippet source</a> | <a href='#snippet-move-static-member-after' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

See more details in [docs/move-static-member.md](docs/move-static-member.md).

### Convert static method to instance method

Convert static method to instance method is the inverse of Make method static: a parameter of a static member function that is a reference to its own class becomes the receiver.

#### Before

<!-- snippet: convert-static-method-to-instance-method-before -->
<a id='snippet-convert-static-method-to-instance-method-before'></a>
```cpp
static double /*|*/total(const Order &order, int qty) {
```
<sup><a href='/tests/cases/convert-to-instance-method/basic/const-reference-receiver/input/main.cpp#L4-L6' title='Snippet source file'>snippet source</a> | <a href='#snippet-convert-static-method-to-instance-method-before' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

#### After

<!-- snippet: convert-static-method-to-instance-method-after -->
<a id='snippet-convert-static-method-to-instance-method-after'></a>
```cpp
double total(int qty) const {
```
<sup><a href='/tests/cases/convert-to-instance-method/basic/const-reference-receiver/expected/main.cpp#L4-L6' title='Snippet source file'>snippet source</a> | <a href='#snippet-convert-static-method-to-instance-method-after' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

See more details in [docs/convert-static-method-to-instance-method.md](docs/convert-static-method-to-instance-method.md).

### Extract object from arguments

Extract object from arguments groups adjacent parameters of a function into a new aggregate type.

#### Before

<!-- snippet: extract-object-from-arguments-before -->
<a id='snippet-extract-object-from-arguments-before'></a>
```cpp
double /*|*/quote(int code, double price, int qty = 1) { return code + price * qty; }
```
<sup><a href='/tests/cases/extract-object-from-arguments/basic/default-omitted-at-some-calls/input/main.cpp#L3-L5' title='Snippet source file'>snippet source</a> | <a href='#snippet-extract-object-from-arguments-before' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

#### After

<!-- snippet: extract-object-from-arguments-after -->
<a id='snippet-extract-object-from-arguments-after'></a>
```cpp
double quote(int code, QuoteLine line) { return code + line.price * line.qty; }
```
<sup><a href='/tests/cases/extract-object-from-arguments/basic/default-omitted-at-some-calls/expected/main.cpp#L8-L10' title='Snippet source file'>snippet source</a> | <a href='#snippet-extract-object-from-arguments-after' title='Start of snippet'>anchor</a></sup>
<!-- endSnippet -->

See more details in [docs/extract-object-from-arguments.md](docs/extract-object-from-arguments.md).

## Using it

```shell
riktig-cpp rename --workspace build/compile_commands.json --file src/foo.cpp --at 12:5 --new-name count
```

See [the command line](docs/command-line.md) for the options, exit codes and refusal codes.

## Building and testing

```shell
./build-and-test
```

For more information, see See [CONTRIBUTING.md](CONTRIBUTING.md).


