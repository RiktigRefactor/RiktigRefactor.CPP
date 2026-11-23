# Rename

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

## Command

Rename the symbol at line 12, column 5 to `count`:

```shell
riktig-cpp rename --workspace build/compile_commands.json --file src/foo.cpp --at 12:5 --new-name count
```

See [the command line](command-line.md) for the options every command accepts.
