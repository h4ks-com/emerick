.PHONY: configure build test run run-release format format-check lint lint-changed quality quality-full fix tidy-fix check-fast docker-build e2e \
	service-check service-run viewer-wasm clean

SOURCES := $(shell git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp')
# The files changed since the last push to main, including unpushed commits, uncommitted edits and new files.
BASE := $(shell git merge-base HEAD origin/main 2>/dev/null || git merge-base HEAD main 2>/dev/null)
CHANGED_FILES := $(shell git diff --name-only $(BASE) 2>/dev/null; git ls-files --others --exclude-standard)
CHANGED := $(sort $(filter $(SOURCES),$(CHANGED_FILES)))
# A changed header can break any file that includes it, so we lint those files too.
CHANGED_HEADERS := $(notdir $(filter %.hpp,$(CHANGED)))
INCLUDERS := $(if $(CHANGED_HEADERS),$(shell git grep -lF $(foreach header,$(CHANGED_HEADERS),-e '$(header)"') -- '*.cpp'))
LINT_CHANGED := $(sort $(CHANGED) $(INCLUDERS))
# The UI and app plugins sit on top of every other layer, so a change only there runs just their tests and the app's.
LEAF_MODULES := src/ui $(wildcard plugins/*)
LEAF_CHANGES := $(filter $(addsuffix /%,$(LEAF_MODULES)),$(CHANGED_FILES))
OTHER_CHANGES := $(filter-out $(LEAF_CHANGES),$(CHANGED_FILES))
FAST_TEST_DIRS := $(if $(and $(LEAF_CHANGES),$(if $(OTHER_CHANGES),,leaf)),app $(LEAF_MODULES))
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc)
# We pin LLVM because every release adds clang-tidy checks and changes formatting.
LLVM_VERSION := 22
ifeq ($(shell uname),Darwin)
QT_PREFIX := $(shell brew --prefix qt 2>/dev/null)
export CMAKE_PREFIX_PATH := $(QT_PREFIX)
LLVM_BIN := $(shell brew --prefix llvm@$(LLVM_VERSION) 2>/dev/null)/bin
TIDY_ARGS := --extra-arg=-isystem$(shell xcrun --show-sdk-path)/usr/include/c++/v1 --extra-arg=-isysroot$(shell xcrun --show-sdk-path)
OTHER_PLATFORM_SOURCES := src/engine/src/miniaudio_backend.cpp
else
# Clang cannot parse libstdc++ 14's ranges::to, so clang-tidy reads libc++ here as it does on macOS.
TIDY_ARGS := --extra-arg=-stdlib=libc++
OTHER_PLATFORM_SOURCES := %_apple.cpp
LLVM_BIN := /usr/lib/llvm-$(LLVM_VERSION)/bin
endif
CLANG_FORMAT ?= $(LLVM_BIN)/clang-format
CLANG_TIDY ?= $(LLVM_BIN)/clang-tidy

configure:
	cmake --preset dev

build: configure
	cmake --build --preset dev

test: build
	ctest --preset dev -j $(JOBS)

run: build
	@bin=build/dev/app/emerick; [ -x $$bin ] || bin=build/dev/app/emerick.app/Contents/MacOS/emerick; exec $$bin $(ARGS)

run-release:
	cmake --preset release
	cmake --build --preset release -j $(JOBS)
	@bin=build/release/app/emerick; [ -x $$bin ] || bin=build/release/app/emerick.app/Contents/MacOS/emerick; exec $$bin $(ARGS)

format:
	$(CLANG_FORMAT) -i $(SOURCES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)

TIDY_FILES = $(filter-out %_test.cpp $(OTHER_PLATFORM_SOURCES),$(filter %.cpp,$(1)))
TIDY = printf '%s\n' $(call TIDY_FILES,$(1)) | xargs -P $(JOBS) -n 4 $(CLANG_TIDY) -p build/dev --quiet $(TIDY_ARGS) $(2)

lint: build
	$(call TIDY,$(SOURCES))

lint-changed: build
	$(if $(call TIDY_FILES,$(LINT_CHANGED)),$(call TIDY,$(LINT_CHANGED)))

quality: format-check lint-changed test

quality-full: format-check lint test

# While working: fix what the tools can fix in the changed files, then check only those files.
fix: build
	$(if $(CHANGED),$(CLANG_FORMAT) -i $(CHANGED))
	$(if $(call TIDY_FILES,$(CHANGED)),$(MAKE) --no-print-directory JOBS=1 tidy-fix)
	$(if $(CHANGED),$(CLANG_FORMAT) -i $(CHANGED))

# One clang-tidy at a time, so two files never fix the same header at once.
tidy-fix:
	$(call TIDY,$(CHANGED),--fix --fix-errors)

check-fast: build
	$(if $(CHANGED),$(CLANG_FORMAT) --dry-run --Werror $(CHANGED))
	$(if $(call TIDY_FILES,$(LINT_CHANGED)),$(call TIDY,$(LINT_CHANGED)))
	$(if $(FAST_TEST_DIRS),$(foreach dir,$(FAST_TEST_DIRS),ctest --test-dir build/dev/$(dir) -j $(JOBS) --output-on-failure --stop-on-failure &&) true,ctest --preset dev -j $(JOBS) --stop-on-failure)

viewer-wasm:
	JOBS=$(JOBS) ./scripts/viewer-wasm.sh build/viewer-wasm

docker-build:
	docker buildx build --load --build-arg JOBS=$(JOBS) $(DOCKER_BUILD_ARGS) -t emerick -f docker/Dockerfile .

e2e: docker-build
	./e2e/run.sh

service-check:
	cd service && uv sync --frozen && uv run ruff format --check . && uv run ruff check . && uv run pyright && uv run pytest

service-run: build
	@bin=$(CURDIR)/build/dev/app/emerick; [ -x $$bin ] || bin=$$bin.app/Contents/MacOS/emerick; \
	cd service && EMERICK_BIN=$${EMERICK_BIN:-$$bin} EMERICK_DATA_DIR=$${EMERICK_DATA_DIR:-$(CURDIR)/build/service} \
	EMERICK_WORK_DIR=$${EMERICK_WORK_DIR:-$(CURDIR)/build/service/sessions} exec uv run emerick-service

clean:
	rm -rf build/dev build/release
