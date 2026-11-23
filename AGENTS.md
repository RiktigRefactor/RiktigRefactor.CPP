This repo implements the C++ side of RiktigRefactor.
The specification is the source of truth: https://github.com/RiktigRefactor/riktigrefactor.spec
Read its `README.md`, `principles.md`, and the page for the refactoring you are changing before you change behavior.
Soundness comes before completeness: refusing is always acceptable, and wrong output never is.

When asked to "build" or "test", run `./build-and-test`.
Also run it automatically before each commit.
Run `./tidy-code` to format the code with `clang-format`.

## Toolchain

The tool is built on clang LibTooling, pinned to LLVM/clang 18 (`CMakeLists.txt` asks for `Clang 18`).
On Ubuntu 24.04, install it with `sudo apt-get install clang-18 clang-format-18 libclang-18-dev llvm-18-dev cmake ninja-build`.
See [UPDATING_DEPENDENCIES.md](UPDATING_DEPENDENCIES.md) for moving to a newer clang.

## Test cases

Cases are directories under `tests/cases/<refactoring>/<group>/<case>/`, in the [format defined by the specification](https://github.com/RiktigRefactor/riktigrefactor.spec/blob/main/test-format.md).
Each holds a `case.json`, an `input/` directory of `.cpp` and `.h` files, and an `expected/` directory unless a refusal is expected.
The runner writes a `compile_commands.json` for the case's `.cpp` files, compiles and runs the input, applies the refactoring, compares with `expected/`, and compiles and runs the output.
Every input and expected file set is a complete program whose `main` returns non-zero if its behavior is wrong.
The cursor is marked with `/*|*/` and a selection with `/*[*/` and `/*]*/`.
To create the `expected/` directory for a new case, run the case once with `RIKTIG_WRITE_EXPECTED=1`, then read the output and confirm it is correct and behavior-preserving before keeping it.
Edge cases set `"naiveShouldFail": true` so the tests prove the assertion can detect the problem: the deliberately naive implementation must produce a program that fails to compile or fails its assertion.
Every case cites the C++ standard sections it covers, by stable name, in its `spec` field (see the README).

Run one case with `build/tests/riktig-cpp-case-runner tests/cases/rename/basic/local-variable`.

## C++

Follow the existing style; `clang-format` decides layout.
The most important thing about a name is that it not be misleading.
It is OK to make the name long to achieve this.
If you see a comment explaining what a function or variable does, rename it to make it more obvious.

## Committing

Before committing, run `./build-and-test`.
If the change has more than one thing going on, split it into multiple commits, each with a clear purpose.
Especially separate refactoring from behavior changes.
See [CONTRIBUTING.md](CONTRIBUTING.md) for the commit notation convention.

## Secrets

Always obscure API keys and other secrets, especially in saved screenshots.

## Cloud sessions

- The pending work for this repo is tracked in the todo list of the claude.ai Project "RiktigRefactor", at `claude/todo.md`.
  Read it at the start of a task, and report what you finished and what is still pending at the end, so it can be updated.
- `gh pr create` is blocked in cloud sessions (it uses GraphQL).
  Open pull requests with `gh api repos/OWNER/REPO/pulls -f title=... -f head=... -f base=main -f body=...`.
- Cloud sessions reach the network only through an allowlisting proxy.
  Hosts needed: the Ubuntu archive (for `apt-get`), and LLVM (`apt.llvm.org`, `github.com` for LLVM release downloads).
- Push to `main` with `git -c push.negotiate=true push origin HEAD:main`.

## After every task

At the end of every task report on all of these, briefly:
- Skills used: name them.
- Skill gaps: if a skill you wished existed would have helped, propose one, even briefly.
- Prompt feedback: one concrete way the request could have been made more effectively this time — skip it only if there's genuinely nothing worth saying.
- Questions, ideas, concerns: surface them as they come up, not just at the end.
- Changes to your skills and instructions that would reduce token usage or improve efficiency.

Treat this end-of-task report as mandatory, like a checklist item — don't let it get crowded out by concise-output-style guidance; it's a small fixed addition, not a violation of terseness.

Never use Claude Code's persistent memory system for this project.
Durable guidance (conventions, process corrections, preferences) belongs in-repo instead — this file or a skill — so it's visible in version control and to every contributor, not just stored privately for one user.

## Decisions

When you need input on a decision:
- First, pick the option you think is best and propose it. Start that message with `⭐ 1: `
- Also list the alternatives you rejected, each with a brief reason why. Start each with `❌ n:`

Then have a discussion.
This formatting applies every time you present a decision, even a quick one — don't let a short or informal-feeling exchange skip it.
