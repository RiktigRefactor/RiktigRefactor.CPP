# Make method static

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

## Command

Make the method at line 3, column 12 static, naming the receiver `order`:

```shell
riktig-cpp make-method-static --workspace build/compile_commands.json --file src/order.h --at 3:12 --receiver-name order
```

See [the command line](command-line.md) for the options every command accepts.
