# Convert static method to instance method

Convert static method to instance method is the inverse of Make method static: a static member function's parameter that is a reference to its own class becomes the receiver:

```cpp
// Before
static double total(const Order &order, int qty) { return order.price * qty; }
double t = Order::total(order, 3);

// After (--receiver order)
double total(int qty) const { return price * qty; }
double t = order.total(3);
```

A `const` reference parameter gives a `const` member function.
`order.member` becomes `member`, and other uses of the parameter become `*this`.
A call through a pointer, `Order::total(*p, 3)`, becomes `p->total(3)`, and a recursive call `total(*order.next, qty)` becomes `next->total(qty)`.
To use a parameter of another class, first move the function into that class with Move static member.

It refuses when, for example:

- the receiver is not the first argument, or it and another argument both may have effects, since the receiver would be evaluated first;
- the parameter is a pointer or a copy;
- a call converts its argument to the class, such as through a converting constructor;
- a lambda captures the parameter by copy;
- the function's address is taken;
- the new member function would override a virtual function of a base class.

Each refusal has a code, listed in [refusal-codes.md](../refusal-codes.md).

See [limitations](https://github.com/RiktigRefactor/riktigrefactor.spec/blob/main/limitations.md) for what is out of scope, such as updating comments and strings.
Names captured as strings change with the rename, by design: `__func__`, `typeid(T).name()`, and `std::source_location::function_name()` report the new name.

## Command

Make the parameter `order` of the static method at line 4, column 19 its receiver:

```shell
riktig-cpp convert-to-instance-method --workspace build/compile_commands.json --file src/order.h --at 4:19 --receiver order
```

See [the command line](command-line.md) for the options every command accepts.
