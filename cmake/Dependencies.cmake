include(FetchContent)
set(FETCHCONTENT_QUIET ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 HttpServer Test)
qt_standard_project_setup(REQUIRES 6.11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)

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

FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG 1.2.9
    GIT_SHALLOW TRUE
    SYSTEM)

# clap-helpers publishes no release tags, so we pin a commit.
FetchContent_Declare(clap_helpers
    GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
    GIT_TAG a380c978f429103e08afc1d17e9efbf3e3c96ee2
    SYSTEM)

FetchContent_MakeAvailable(nlohmann_json readerwriterqueue libremidi sol2 Catch2 clap clap_helpers)

# miniaudio ships a single source file; we compile it once as our own target. macOS talks to CoreAudio directly.
if(NOT APPLE)
    FetchContent_GetProperties(miniaudio)
    if(NOT miniaudio_POPULATED)
        FetchContent_Populate(miniaudio)
    endif()
    add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
    target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# Package managers already ship Lua 5.5, which sol2 does not support, so we build 5.4 from source.
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae)
FetchContent_MakeAvailable(lua)

# The General MIDI instrument plays GeneralUser GS v2.0.3, which its license lets us redistribute.
set(generaluser_gs_commit 684543d5e5efaef08d02be50dcda8d552478fa60)
FetchContent_Declare(generaluser_gs
    URL https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${generaluser_gs_commit}/GeneralUser-GS.sf2
    URL_HASH SHA256=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_Declare(generaluser_gs_license
    URL https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${generaluser_gs_commit}/documentation/LICENSE.txt
    URL_HASH SHA256=7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(generaluser_gs generaluser_gs_license)
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

# We compile tree-sitter and its Lua grammar from their C sources, so SOURCE_SUBDIR names a folder without a
# CMakeLists.txt and their own builds stay out.
FetchContent_Declare(tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.26.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(tree_sitter_lua
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-lua.git
    GIT_TAG v0.5.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(tree_sitter tree_sitter_lua)
add_library(tree_sitter STATIC ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(tree_sitter SYSTEM PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include)
target_include_directories(tree_sitter PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src)
target_compile_definitions(tree_sitter PRIVATE _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
add_library(tree_sitter_lua STATIC ${tree_sitter_lua_SOURCE_DIR}/src/parser.c ${tree_sitter_lua_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_lua PRIVATE ${tree_sitter_lua_SOURCE_DIR}/src)
target_link_libraries(tree_sitter_lua PUBLIC tree_sitter)
set_target_properties(tree_sitter tree_sitter_lua PROPERTIES C_STANDARD 11)
