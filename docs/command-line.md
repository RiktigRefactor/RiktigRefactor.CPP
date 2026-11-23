# Command line

```shell
riktig-cpp <refactoring> --workspace build/compile_commands.json --file src/foo.cpp --at 12:5 [refactoring options]
```

Positions are 1-based, and columns count Unicode scalar values.
`--workspace` accepts `compile_commands.json` or the directory holding it; the tool needs it to resolve includes and macros, and refuses to guess without it.
CMake writes it with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

- `--apply` writes the changes to disk.
- `--json` prints the result using the shared protocol, without the diff.

The exit code is 0 on success, 1 when the refactoring is refused, and 2 on an error such as an unknown file.
Each refusal has a code, listed in [refusal codes](../refusal-codes.md).

There are no release binaries yet; build from a clone and run `build/riktig-cpp`.

## Refactorings

- [Rename](rename.md): `rename`
- [Extract IIFE](extract-iife.md): `extract-iife`
- [Make method static](make-method-static.md): `make-method-static`
- [Move static member](move-static-member.md): `move-static-member`
- [Move static method to instance method](move-static-method-to-instance-method.md): `move-to-instance-method`
- [Extract object from arguments](extract-object-from-arguments.md): `extract-object-from-arguments`
