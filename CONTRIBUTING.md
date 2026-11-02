# Contributing

How to build Emerick, check a change and send it.

## Build

```
brew install qt cmake ninja ccache libsndfile fluid-synth lua pkgconf llvm@22 uv
make build
make test
```

On Ubuntu 24.04 the CI job in `.github/workflows/ci.yml` lists the packages.

On Windows, in an [MSYS2](https://www.msys2.org) UCRT64 shell:

```
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,ccache,pkgconf,libsndfile,fluidsynth,qt6-base,qt6-declarative,qt6-httpserver,qt6-websockets}
cmake --preset release
cmake --build --preset release
```

## Make targets

The Makefile is the single interface, and CI calls only these targets.

| Target | What it does |
|---|---|
| `make build` | configure and build the `dev` preset |
| `make test` | build and run the unit tests |
| `make check-fast` | format-check and lint the changed files, build, run the affected tests |
| `make fix` | clang-format and clang-tidy --fix on the changed files |
| `make quality` | format-check everything, lint the files changed since `origin/main`, test everything |
| `make quality-full` | format-check, lint and test everything, as CI does |
| `make run-release` | build the `release` preset and run it |
| `make service-check` | ruff format check, ruff lint, pyright strict and pytest for `service/` |
| `make service-run` | run the session service on the `dev` build, with data in `build/service` |
| `make docker-build` | build the image: the release build and its tests, then the runtime |
| `make e2e` | build the image and drive the headless app and the service over MCP |

## Before a pull request

```
make quality
make service-check
make e2e
```

Fix every failure. Never weaken a check, a warning flag or a test to make it pass.

## Commits

One line, conventional commits, no body: `feat: add clip fades`, `fix: keep loop length on undo`.

## Layers and style

Read `AGENTS.md`, `docs/architecture.md` and `docs/CODE_STYLE.md` before editing.

## Run the service locally

```
make build
make service-run
curl -s localhost:8080/health
```

`service/README.md` lists its settings and endpoints.
