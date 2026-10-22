cmake_minimum_required(VERSION 3.28)
project(emerick VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# We use headers only, and GCC's module scanning flags break clang-tidy.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EMERICK_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

if(EMERICK_BUILD_TESTS)
    enable_testing()
    include(Catch)
endif()

add_subdirectory(src/core)
add_subdirectory(src/model)
add_subdirectory(src/engine)
add_subdirectory(src/app)
add_subdirectory(src/ui)
add_subdirectory(plugins/yamaha)
add_subdirectory(app)
