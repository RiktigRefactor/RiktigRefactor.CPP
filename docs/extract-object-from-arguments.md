# Extract object from arguments

Extract object from arguments groups adjacent parameters of a function into a new aggregate type, and updates the declaration, the definition, every call and the uses in the body:

```cpp
// Before
int total(int base, int count, int step);
int r = total(1, 3, 2);

// After
struct Steps {
    int count;
    int step;
};

int total(int base, Steps steps);
int r = total(1, Steps{.count = 3, .step = 2});
```

Uses of a grouped parameter in the body become `steps.count`, and a recursive call builds the object from the members.
It refuses when, for example:

- the grouped parameters are not next to each other;
- a grouped parameter is a reference, an array, a function pointer, `volatile`, or has a non-trivial destructor;
- the function is virtual, or its address is taken;
- the arguments have side effects, or one would be a narrowing conversion;
- the new type name is already used.

Each refusal has a code, listed in [refusal codes](../refusal-codes.md).

## Command

Group the parameters `count` and `step` of the function at line 3, column 5 into a type named `Steps`:

```shell
riktig-cpp extract-object-from-arguments --workspace build/compile_commands.json --file src/total.h --at 3:5 --parameters count,step --type-name Steps
```

`--parameter-name` names the new parameter.
