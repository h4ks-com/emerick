# AGENTS.md

Instructions for agents working in this repo. Read before editing.

## What this is
Emerick is a small native DAW in C++23 and Qt 6.11 QML. It records and plays audio and MIDI on tracks of clips, and every action is reachable by a person, the Cmd+K palette, Lua and an agent over MCP through one registry. Targets: macOS first, Linux, Windows.

## Where things live
- How code is written (types at the boundary, realtime rules, QML rules, names): `docs/CODE_STYLE.md`
- Architecture and layer rules: `docs/architecture.md`
- Visual design tokens and layout: `docs/design.md`
- Layers: `src/core`, `src/model`, `src/engine` (no Qt), `src/app` (Qt glue, registry, MCP, Lua), `src/ui` (QML), `src/gm` (the bundled General MIDI CLAP instrument), `src/voices` (the bundled Expansion Voices CLAP instrument), `src/arranger` (the Yamaha style arranger engine and the bundled Arranger CLAP device, no Qt), `plugins/<name>`, `app/` (executable)
- Tests sit next to their module in `tests/`, one Catch2 binary per module

## Commands (the Makefile is the single interface)
- `make build` configure and build with the `dev` preset
- `make test` build and run all unit tests
- `make run ARGS="--audio=null --midi=virtual"` run the app
- `make run-release` build with the `release` preset and run the optimized app
- `--profile <folder>` keeps settings and new projects in that folder; every test or agent run passes one
- Tests run offscreen and never show a window; agents run the app only inside a VM, never on the machine they build on
- `make format` / `make format-check` clang-format
- `make lint` clang-tidy, warnings are errors
- `make fix` clang-format and clang-tidy --fix on the files the branch changed
- `make check-fast` format-check and lint the changed files, build, run the tests in parallel; use it while working
- `make quality` the full gate: format-check, lint, test; run it once before a commit
- `--headless` runs the app with no window, driven over MCP only, so it needs `--mcp-port` or the `mcp.port` setting; MCP then leaves out the `ui.*` tools and `screenshot`, and window only actions return an error
- `--script <file> [-- args...]` runs one Lua script with no window and exits, 1 on a script error; the script reads `arg` and can call `emerick.wait`, and MCP runs only with `--mcp-port`; `docker run --rm emerick-dev --script <file>` runs it in the image
- `make docker-build` build the headless image (Qt 6.11, builds the repo, runs ctest)
- `make e2e` build the Docker image and drive the headless app over MCP
- `docker run --rm -p 127.0.0.1:8888:8888 emerick-dev` run the headless app; `.mcp.json` registers it as the `emerick` MCP server

## Before considering work complete
1. Run `make quality` and fix every failure.
2. Never weaken a check, a warning flag or a test to make it pass.
3. Leave no dead code, unused includes or unused dependencies.

## Layer rules
- `core`, `model` and `engine` never include Qt. `engine` never includes `model`.
- The audio thread never allocates, locks, logs or frees.
- State has one home: the project lives in `model::Document`, every UI-reachable action and setting lives in `app::Registry`. Never add a second list of actions, settings or track kinds anywhere.
- Every interactive QML control sets `objectName` to its registry id.
- Address tracks, clips and devices by id, never by index.

## Code style
- C++23. RAII, value types, `std::unique_ptr` for ownership, no raw `new`/`delete`.
- Small free functions and plain structs first. An interface needs two real implementations (a test double counts) before it exists.
- Names explain the code. Default to zero comments; keep one only for a non-obvious why, as one plain sentence.
- No catch-all `catch (...)`; catch the exceptions that can actually happen.
- Prose (docs, commits, UI text): plain, declarative, no em dashes. UI text is imperative ("Add audio track").

## Git
- Work in a worktree branch per task. Commit messages are one line, conventional commits (`feat: add clip fades`), no body, no trailers.
- Run `make quality` before every commit, and the `/rev` review when asked or for risky changes.
