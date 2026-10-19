.PHONY: configure build test run format format-check lint quality docker-build e2e clean

SOURCES := $(shell git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp')
ifeq ($(shell uname),Darwin)
QT_PREFIX := $(shell brew --prefix qt 2>/dev/null)
export CMAKE_PREFIX_PATH := $(QT_PREFIX)
TIDY_ARGS := --extra-arg=-isystem$(shell xcrun --show-sdk-path)/usr/include/c++/v1 --extra-arg=-isysroot$(shell xcrun --show-sdk-path)
OTHER_PLATFORM_SOURCES := src/engine/src/miniaudio_backend.cpp
else
OTHER_PLATFORM_SOURCES := %_apple.cpp
endif

configure:
	cmake --preset dev

build: configure
	cmake --build --preset dev

test: build
	ctest --preset dev

run: build
	@bin=build/dev/app/emerick; [ -x $$bin ] || bin=build/dev/app/emerick.app/Contents/MacOS/emerick; exec $$bin $(ARGS)

format:
	clang-format -i $(SOURCES)

format-check:
	clang-format --dry-run --Werror $(SOURCES)

lint: build
	clang-tidy -p build/dev --quiet $(TIDY_ARGS) $(filter-out %_test.cpp $(OTHER_PLATFORM_SOURCES),$(filter %.cpp,$(SOURCES)))

quality: format-check lint test

docker-build:
	docker build -t emerick-dev -f docker/Dockerfile .

e2e: docker-build
	./e2e/run.sh

clean:
	rm -rf build/dev build/release
