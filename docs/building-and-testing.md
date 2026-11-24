# Building and testing

The tool is pinned to clang 23.
Install [mise](https://mise.jdx.dev/); it provides clang/LLVM 23, cmake and ninja:

```shell
./build-and-test
```

`build-and-test` builds and runs every test case; `build-and-test.cmd` does the same on Windows through Git for Windows' Bash, but the build is only tested on Linux.
Run `./tidy-code` to format the code.
See [UPDATING_DEPENDENCIES.md](../UPDATING_DEPENDENCIES.md) for moving to a newer clang.

Test cases live in `tests/cases`, in the [format defined by the specification](https://github.com/RiktigRefactor/riktigrefactor.spec/blob/main/test-format.md).
Every input and expected program is compiled and run, and edge cases must fail under a deliberately naive implementation.

### Citing the C++ standard

Every test case lists the sections of the C++ standard it covers in the `spec` field of its `case.json`, by stable name, written as `C++ [<name>]`, for example `C++ [over.match]`.
Stable names follow the C++20 standard (ISO/IEC 14882:2020) as listed at [eel.is/c++draft](https://eel.is/c++draft/).

## Limitations

See [limitations](https://github.com/RiktigRefactor/riktigrefactor.spec/blob/main/limitations.md) for what is out of scope, such as updating comments and strings.
Names captured as strings change with the rename, by design: `__func__`, `typeid(T).name()`, and `std::source_location::function_name()` report the new name.
