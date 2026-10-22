.PHONY: configure build test run format format-check lint quality docker-build e2e clean

SOURCES := $(shell git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp')
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
	ctest --preset dev

run: build
	@bin=build/dev/app/emerick; [ -x $$bin ] || bin=build/dev/app/emerick.app/Contents/MacOS/emerick; exec $$bin $(ARGS)

format:
	$(CLANG_FORMAT) -i $(SOURCES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)

lint: build
	$(CLANG_TIDY) -p build/dev --quiet $(TIDY_ARGS) $(filter-out %_test.cpp $(OTHER_PLATFORM_SOURCES),$(filter %.cpp,$(SOURCES)))

quality: format-check lint test

docker-build:
	docker build -t emerick-dev -f docker/Dockerfile .

e2e: docker-build
	./e2e/run.sh

clean:
	rm -rf build/dev build/release
