# Updating dependencies

Dependabot updates the GitHub Actions, the dotnet tools in `dotnet-tools.json`, and the .NET SDK in `global.json`.
Everything else is updated by hand.

## clang and LLVM

The tool is pinned to clang 18, the version Ubuntu 24.04 ships, because the tests verify its AST and binding behavior.
To move to a newer version:

1. Change `RIKTIG_CLANG_VERSION` in `CMakeLists.txt`.
1. Change the package names in `.github/workflows/build-and-test.yml`, `.github/workflows/tidy-code.yml`, `build-and-test`, `tidy-code`, and the toolchain section of `AGENTS.md`.
1. Run `./build-and-test` and fix what the new version changes.
   Commit it as `^ r`.
