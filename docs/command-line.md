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
- [Convert static method to instance method](convert-static-method-to-instance-method.md): `convert-to-instance-method`
- [Extract object from arguments](extract-object-from-arguments.md): `extract-object-from-arguments`

## Language server

```shell
riktig-cpp lsp
```

Runs a language server on standard input and output, for editors such as the [VS Code extension](https://github.com/RiktigRefactor/RiktigRefactor.VSCode).
It offers Rename as a code action, and runs it when the client sends `workspace/executeCommand` with `riktig.refactor` and a protocol request.
It finds `compile_commands.json` in the workspace folder or its `build` folder, or takes `initializationOptions.workspace`.
It reads files from disk and loads the workspace for every request, so the client must save files first.
The mapping is defined in the [specification](https://github.com/RiktigRefactor/riktigrefactor.spec/blob/main/interfaces.md#lsp-mapping).
