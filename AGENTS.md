# AGENTS.md

Instructions for agents working in this repo. Read before editing.

## What this is
Emerick is a small native DAW in C++23 and Qt 6.11 QML. It records and plays audio and MIDI on tracks of clips, and every action is reachable by a person, the Cmd+K palette, Lua and an agent over MCP through one registry. Targets: macOS first, Linux, Windows.

## Where things live
- How code is written (types at the boundary, realtime rules, QML rules, names): `docs/CODE_STYLE.md`
- Architecture and layer rules: `docs/architecture.md`
- Visual design tokens and layout: `docs/design.md`
- Layers: `src/core`, `src/model`, `src/engine` (no Qt), `src/app` (Qt glue, registry, MCP, Lua), `src/ui` (QML), `src/viewer` (the read-only viewer, built for WebAssembly), `src/gm` (the bundled General MIDI CLAP instrument), `src/voices` (the bundled Expansion Voices CLAP instrument), `src/sampler` (the bundled Emerick Sampler CLAP instrument, SFZ through sfizz, no Qt), `src/arranger` (the Yamaha style arranger engine and the bundled Arranger CLAP device, no Qt), `plugins/<name>`, `app/` (executable)
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
- `make check-fast` format-check and lint the changed files, build, run the tests in parallel (only the UI, plugin and app tests when only `src/ui` or `plugins` changed); use it while working
- `make quality` the gate: format-check everything, lint the files changed since `origin/main` and the files that include a changed header, run every test; run it once before a commit
- `make quality-full` lints every file; CI runs it nightly, and a push runs `make quality` against the commit before it
- `--headless` runs the app with no window, driven over MCP only, so it needs `--mcp-port` or the `mcp.port` setting; MCP then leaves out the `ui.*` tools and `screenshot`, and window only actions return an error
- `--script <file> [-- args...]` runs one Lua script with no window and exits, 1 on a script error; the script reads `arg` and can call `emerick.wait`, and MCP runs only with `--mcp-port`; `docker run --rm emerick --script <file>` runs it in the image
- `--agent-read-root`, `--agent-write-root`, `--no-lua-mcp`, `--lua-timeout-ms`, `--lua-memory-mb`, `--max-render-seconds` and `--bundled-plugins-only` limit what agents may do in a service run; `state.get` with `limits` lists the active ones and `--help` explains each
- `--install-library <id>` (repeatable, or `all`) installs a sound library with no window and exits, 1 on a failure; `EMERICK_LIBRARY_DIR` moves the library folder, which a `--profile` run keeps in the profile, and a `--headless` run refuses `library.install` and `library.remove` unless `EMERICK_LIBRARY_INSTALL=1`
- `video.render` needs `ffmpeg` beside the app or on PATH (Homebrew's on macOS); the image ships a minimal static one in `/opt/emerick`
- `make viewer-wasm` build the read-only viewer for WebAssembly into `build/viewer-wasm/site`; it installs Qt for WebAssembly and emsdk into `~/.cache/emerick/wasm-toolchain` once
- `make docker-build` build the image (Qt 6.11, builds the `release` preset, then a slim runtime with the service)
- `make e2e` build the Docker image and drive the headless app and the service over MCP
- `make service-check` ruff, pyright strict and pytest for the session service in `service/`
- `make release VERSION=x.y.z` set the version in `VERSION`, its only home, and commit the bump locally; pushing stays a step you run yourself
- `make publish` wait for green CI on the pushed `origin/main` and create the GitHub release for `VERSION`; `NOTES` (text or a file) replaces the generated notes
- `docker run --rm -p 127.0.0.1:8080:8080 emerick` run the Emerick service, which starts one headless app per session (`service/README.md`)
- `docker run --rm -p 127.0.0.1:8888:8888 emerick app` run one headless app; `.mcp.json` registers it as the `emerick` MCP server

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
