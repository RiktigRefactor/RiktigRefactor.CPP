# Updating dependencies

Dependabot updates the GitHub Actions, the dotnet tools in `dotnet-tools.json`, and the .NET SDK in `global.json`.
Everything else is updated by hand.

## clang and LLVM

The tool is pinned to the clang version in `.mise.toml` because the tests verify its AST and binding behavior.
To move to a newer version:

1. Change the `conda:clangdev` and `conda:clang-format` versions in `.mise.toml`; `CMakeLists.txt` reads the version from there.
1. Check that `.github/workflows/build-and-test.yml`, `.github/workflows/tidy-code.yml`, `build-and-test` and `tidy-code` name no version.
1. Run `./build-and-test` and fix what the new version changes.
   Commit it as `^ r`.
