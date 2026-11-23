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
    * [Move static method to instance method](#move-static-method-to-instance-method)
    * [Extract object from arguments](#extract-object-from-arguments)
  * [Using it](#using-it)
  * [Building and testing](#building-and-testing)
  * [Contributing](#contributing)<!-- endToc -->

## What it can do

The tool has these refactorings:

### Rename

Rename renames a symbol and every reference to it across every translation unit in `compile_commands.json`, including the headers they include.

#### Before

```cpp
struct Order {
    int qty;
    int total() const { return qty * 2; }
};
int t = Order{3}.total();
```

#### After

```cpp
struct Order {
    int quantity;
    int total() const { return quantity * 2; }
};
int t = Order{3}.total();
```

See more details in [docs/rename.md](docs/rename.md).

### Extract IIFE

Extract IIFE wraps a selected expression, or whole statements of one block, in an immediately-invoked lambda.

#### Before

```cpp
auto total = price * qty;
```

#### After

```cpp
auto total = [](double price, int qty) { return price * qty; }(price, qty);
```

See more details in [docs/extract-iife.md](docs/extract-iife.md).

### Make method static

Make method static turns an instance member function into a static one, with the receiver as its first parameter.

#### Before

```cpp
double total(int qty) const { return price * qty; }
double t = order.total(3);
```

#### After

```cpp
static double total(const Order &self, int qty) { return self.price * qty; }
double t = Order::total(order, 3);
```

See more details in [docs/make-method-static.md](docs/make-method-static.md).

### Move static member

Move static member moves a static member function or `constexpr` static data member to another class, or a namespace-scope function or `constexpr` variable to another namespace, and updates every reference.

#### Before

```cpp
struct Order {
    static double total(const Order &self, int qty);
};
double t = Order::total(order, 3);
```

#### After

```cpp
struct Pricing {
    static double total(const Order &self, int qty);
};
double t = Pricing::total(order, 3);
```

See more details in [docs/move-static-member.md](docs/move-static-member.md).

### Move static method to instance method

Move static method to instance method is the inverse of Make method static: a parameter of a static member function that is a reference to its own class becomes the receiver.

#### Before

```cpp
static double total(const Order &order, int qty) { return order.price * qty; }
double t = Order::total(order, 3);
```

#### After

```cpp
double total(int qty) const { return price * qty; }
double t = order.total(3);
```

See more details in [docs/move-static-method-to-instance-method.md](docs/move-static-method-to-instance-method.md).

### Extract object from arguments

Extract object from arguments groups adjacent parameters of a function into a new aggregate type.

#### Before

```cpp
int total(int base, int count, int step);
int r = total(1, 3, 2);
```

#### After

```cpp
struct Steps {
    int count;
    int step;
};

int total(int base, Steps steps);
int r = total(1, Steps{.count = 3, .step = 2});
```

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

See [building and testing](docs/building-and-testing.md) for the toolchain, the test cases, and how they cite the C++ standard.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the commit message convention, and [AGENTS.md](AGENTS.md) for how the repo is organized.
