# Move static member

Move static member moves a static member function or `constexpr` static data member to another class, or a namespace-scope function or `constexpr` variable to another namespace, and updates every reference:

```cpp
// Before
struct Order {
    static double total(const Order &self, int qty) { return self.quantity * qty * rate; }
};
double t = Order::total(order, 3);

// After (--target Pricing)
struct Pricing {
  public:
    static double total(const Order &self, int qty) { return self.quantity * qty * Order::rate; }
};
double t = Pricing::total(order, 3);
```

The declaration moves to the end of the target class, under its current access specifier, or to the end of the target namespace's block in the same file.
An out-of-line definition stays where it is, with the target as its qualifier.
Names in the moved code that were found through the source's scope are qualified, so they still name the same declarations.
Together with Make method static, this moves a method to another class.

It refuses when, for example:

- the target, or a base class of it, already has a member with that name (other than overloads that keep every call's choice);
- a call would choose another overload after the move;
- a variable is not `constexpr`, so when it is initialized would depend on where it is defined;
- a reference goes through an expression with an effect, such as `make().total()`;
- the moved code would not compile in the target, for example because it uses a private member of the source or a name declared after the target;
- the move is between a class and a namespace (not supported yet).

## Command

Move the static member at line 9, column 19 to the class `shop::Pricing`:

```shell
riktig-cpp move-static-member --workspace build/compile_commands.json --file src/order.h --at 9:19 --target shop::Pricing
```

See [the command line](command-line.md) for the options every command accepts.
