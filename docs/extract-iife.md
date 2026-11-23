# Extract IIFE

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

## Command

Preview an Extract IIFE of the text from line 12, column 19 up to (not including) column 30:

```shell
riktig-cpp extract-iife --workspace build/compile_commands.json --file src/foo.cpp --from 12:19 --to 12:30
```

See [the command line](command-line.md) for the options every command accepts.
