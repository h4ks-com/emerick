# AGENTS.md

Instructions for agents working in this repo. Read before editing.

## What this is
Emerick is a small native DAW in C++23 and Qt 6.11 QML. It records and plays audio and MIDI on tracks of clips, and every action is reachable by a person, the Cmd+K palette, Lua and an agent over MCP through one registry. Targets: macOS first, Linux, Windows.

## Where things live
- How code is written (types at the boundary, realtime rules, QML rules, names): `docs/CODE_STYLE.md`
- Architecture and layer rules: `docs/architecture.md`
- Visual design tokens and layout: `docs/design.md`
- Layers: `src/core`, `src/model`, `src/engine` (no Qt), `src/app` (Qt glue, registry, MCP, Lua), `src/ui` (QML), `plugins/<name>`, `app/` (executable)
- Tests sit next to their module in `tests/`, one Catch2 binary per module

## Commands (the Makefile is the single interface)
- `make build` configure and build with the `dev` preset
- `make test` build and run all unit tests
- `make run ARGS="--audio=null --midi=virtual"` run the app
- `--profile <folder>` keeps settings and new projects in that folder; every test or agent run passes one
- `make format` / `make format-check` clang-format
- `make lint` clang-tidy, warnings are errors
- `make quality` the full gate: format-check, lint, test
- `make docker-build` build the headless image (Qt 6.11, builds the repo, runs ctest)
- `make e2e` build the Docker image and drive the headless app over MCP
- `docker run --rm -p 127.0.0.1:7777:7777 emerick-dev` run the headless app; `.mcp.json` registers it as the `emerick` MCP server

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
- Run `make quality` and the `/rev` review before every commit.
