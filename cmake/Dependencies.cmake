include(FetchContent)
set(FETCHCONTENT_QUIET ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 HttpServer Test)
qt_standard_project_setup(REQUIRES 6.11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE)

FetchContent_Declare(readerwriterqueue
    GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
    GIT_TAG v1.0.7
    GIT_SHALLOW TRUE)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE)

set(LIBREMIDI_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_TESTS OFF CACHE BOOL "" FORCE)
# libremidi picks its byte container by whether Boost is on the include path, so a target that sees Homebrew's Boost
# and one that does not would disagree on the message layout; we keep it on std::vector everywhere.
set(LIBREMIDI_NO_BOOST ON CACHE BOOL "" FORCE)
if(APPLE)
    set(LIBREMIDI_NO_JACK ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(libremidi
    GIT_REPOSITORY https://github.com/celtera/libremidi.git
    GIT_TAG v5.4.3
    GIT_SHALLOW TRUE)

set(SOL2_LUA_VERSION "5.4" CACHE STRING "" FORCE)
FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.5.0
    GIT_SHALLOW TRUE)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(nlohmann_json readerwriterqueue libremidi sol2 Catch2)

# miniaudio ships a single source file; we compile it once as our own target.
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()
add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
if(APPLE)
    target_link_libraries(miniaudio PUBLIC "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation")
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# Package managers already ship Lua 5.5, which sol2 does not support, so we build 5.4 from source.
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae)
FetchContent_MakeAvailable(lua)
file(GLOB lua_sources ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM lua_sources ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
add_library(lua54 STATIC ${lua_sources})
# sol2 includes <lua/lua.hpp> first, so we publish the headers under lua/ and link lua54 before Qt, whose
# include path can carry another Lua's lua/ folder.
file(COPY ${lua_SOURCE_DIR}/src/lua.h ${lua_SOURCE_DIR}/src/luaconf.h ${lua_SOURCE_DIR}/src/lualib.h
    ${lua_SOURCE_DIR}/src/lauxlib.h ${lua_SOURCE_DIR}/src/lua.hpp DESTINATION ${CMAKE_BINARY_DIR}/lua54/include/lua)
target_include_directories(lua54 SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/lua54/include)
if(UNIX)
    target_compile_definitions(lua54 PRIVATE LUA_USE_POSIX)
endif()
