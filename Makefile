.PHONY: configure build test run run-release format format-check lint quality fix tidy-fix check-fast docker-build e2e clean

SOURCES := $(shell git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp')
# The C++ files a branch changed since it left main, plus uncommitted and new ones.
CHANGED := $(sort $(filter $(SOURCES),$(shell git diff --name-only $$(git merge-base HEAD main) 2>/dev/null; git ls-files --others --exclude-standard)))
JOBS ?= 4
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

quality: format-check lint test

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
	$(if $(call TIDY_FILES,$(CHANGED)),$(call TIDY,$(CHANGED)))
	ctest --preset dev -j $(JOBS) --stop-on-failure

docker-build:
	docker build -t emerick-dev -f docker/Dockerfile .

e2e: docker-build
	./e2e/run.sh

clean:
	rm -rf build/dev build/release
