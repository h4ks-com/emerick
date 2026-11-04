include(FetchContent)
set(FETCHCONTENT_QUIET ON)

if(EMERICK_VIEWER)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)
else()
    find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick QuickControls2 HttpServer Network Test)
endif()
qt_standard_project_setup(REQUIRES 6.11)

if(EMSCRIPTEN)
    # nlohmann_json's serializer trips our sign conversion warning against emscripten's libc++.
    set(nlohmann_json_system SYSTEM)
endif()
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
    ${nlohmann_json_system})

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)

if(EMERICK_VIEWER)
    FetchContent_MakeAvailable(nlohmann_json)
    if(EMERICK_BUILD_TESTS)
        FetchContent_MakeAvailable(Catch2)
    endif()
    return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)

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
# Windows reaches MIDI devices through WinMM; the WinRT back-ends need C++/WinRT headers that MinGW lacks.
if(WIN32)
    set(LIBREMIDI_NO_WINUWP ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_WINMIDI ON CACHE BOOL "" FORCE)
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

FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_MakeAvailable(nlohmann_json readerwriterqueue libremidi sol2 Catch2 clap clap_helpers pugixml)

# The VST3 SDK's headers include each other from the folder that holds pluginterfaces and public.sdk, so both land in
# one folder. We build only the hosting parts we use, as their own target without our warnings.
set(vst3_sdk_tag v3.8.1_build_84)
set(vst3_sdk_folder ${FETCHCONTENT_BASE_DIR}/vst3sdk)
FetchContent_Declare(vst3_pluginterfaces
    URL https://github.com/steinbergmedia/vst3_pluginterfaces/archive/refs/tags/${vst3_sdk_tag}.tar.gz
    URL_HASH SHA256=19e88e225b6ecb4a3c429d3ff2fd511be9c6c8b8b1c7e86b05949b45c0553e2d
    SOURCE_DIR ${vst3_sdk_folder}/pluginterfaces
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(vst3_public_sdk
    URL https://github.com/steinbergmedia/vst3_public_sdk/archive/refs/tags/${vst3_sdk_tag}.tar.gz
    URL_HASH SHA256=f683db0c831dd6603a1bb8c7747a6e1dd223ac3423e288b08c5706cf30b76ae8
    SOURCE_DIR ${vst3_sdk_folder}/public.sdk
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(vst3_pluginterfaces vst3_public_sdk)
set(vst3_hosting ${vst3_sdk_folder}/public.sdk/source/vst/hosting)
add_library(vst3_sdk STATIC
    ${vst3_sdk_folder}/pluginterfaces/base/coreiids.cpp
    ${vst3_sdk_folder}/pluginterfaces/base/funknown.cpp
    ${vst3_sdk_folder}/public.sdk/source/common/commoniids.cpp
    ${vst3_sdk_folder}/public.sdk/source/common/commonstringconvert.cpp
    ${vst3_sdk_folder}/public.sdk/source/common/memorystream.cpp
    ${vst3_sdk_folder}/public.sdk/source/vst/vstinitiids.cpp
    ${vst3_sdk_folder}/public.sdk/source/vst/utility/stringconvert.cpp
    ${vst3_sdk_folder}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp
    ${vst3_hosting}/hostclasses.cpp
    ${vst3_hosting}/module.cpp
    ${vst3_hosting}/pluginterfacesupport.cpp)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(vst3_sdk PRIVATE ${vst3_hosting}/module_mac.mm)
    set_source_files_properties(${vst3_hosting}/module_mac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(vst3_sdk PUBLIC "-framework Cocoa")
elseif(WIN32)
    target_sources(vst3_sdk PRIVATE ${vst3_hosting}/module_win32.cpp)
else()
    target_sources(vst3_sdk PRIVATE ${vst3_hosting}/module_linux.cpp)
    target_link_libraries(vst3_sdk PUBLIC ${CMAKE_DL_LIBS})
endif()
target_include_directories(vst3_sdk SYSTEM PUBLIC ${vst3_sdk_folder})
target_compile_options(vst3_sdk PRIVATE -w)
# The test plugins link the SDK into shared modules.
set_target_properties(vst3_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The expansion voices instrument is a shared module, so the static pugixml it links must be position independent.
set_target_properties(pugixml-static PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

# Package managers already ship Lua 5.5, which sol2 does not support, so we build 5.4 from source. We take it from
# Lua's GitHub repository, which keeps the sources in one folder, with its tests and amalgamation beside them.
FetchContent_Declare(lua
    URL https://github.com/lua/lua/archive/refs/tags/v5.4.8.tar.gz
    URL_HASH SHA256=d85b70a65f43c5d2254944d58d625e822c8e2e10d9c6a3bd9b5b657e46376a19)
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
file(GLOB lua_sources ${lua_SOURCE_DIR}/*.c)
list(REMOVE_ITEM lua_sources ${lua_SOURCE_DIR}/lua.c ${lua_SOURCE_DIR}/ltests.c ${lua_SOURCE_DIR}/onelua.c)
add_library(lua54 STATIC ${lua_sources})
# sol2 includes <lua/lua.hpp> first, so we publish the headers under lua/ and link lua54 before Qt, whose
# include path can carry another Lua's lua/ folder. Lua's release archives add lua.hpp, which its repository lacks.
file(COPY ${lua_SOURCE_DIR}/lua.h ${lua_SOURCE_DIR}/luaconf.h ${lua_SOURCE_DIR}/lualib.h ${lua_SOURCE_DIR}/lauxlib.h
    DESTINATION ${CMAKE_BINARY_DIR}/lua54/include/lua)
file(WRITE ${CMAKE_BINARY_DIR}/lua54/include/lua/lua.hpp
    "extern \"C\" {\n#include \"lua.h\"\n#include \"lualib.h\"\n#include \"lauxlib.h\"\n}\n")
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

# The Emerick Sampler plays SFZ instruments through libsfizz, linked statically with the libraries its release bundles.
# We build it as C++17 in a function scope, the standard it is tested with, and leave out its clients and OpenMP.
FetchContent_Declare(sfizz
    URL https://github.com/sfztools/sfizz/releases/download/1.2.3/sfizz-1.2.3.tar.gz
    URL_HASH SHA256=a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822
    EXCLUDE_FROM_ALL
    SYSTEM)
function(emerick_add_sfizz)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_DISABLE_FIND_PACKAGE_OpenMP ON)
    # Unoptimized, sfizz takes minutes to decode the start of every sample in a large library.
    if(NOT MSVC)
        set(CMAKE_C_FLAGS_DEBUG "-O2 -g")
        set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g")
    endif()
    # sfizz passes 32-bit ARM flags to every processor whose name starts with arm, which Apple's arm64 refuses.
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(PROJECT_SYSTEM_PROCESSOR aarch64 CACHE STRING "" FORCE)
    endif()
    set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
    set(SFIZZ_JACK OFF CACHE BOOL "" FORCE)
    set(SFIZZ_RENDER OFF CACHE BOOL "" FORCE)
    set(SFIZZ_SHARED OFF CACHE BOOL "" FORCE)
    set(SFIZZ_GIT_SUBMODULE_CHECK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sfizz)
endfunction()
emerick_add_sfizz()
# sfizz turns its asserts and debug prints on in every build type, and they print from the audio thread.
foreach(target sfizz_internal sfizz_parser sfizz_messaging sfizz_import sfizz_static)
    foreach(property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(definitions ${target} ${property})
        if(definitions)
            list(REMOVE_ITEM definitions SFIZZ_ENABLE_RELEASE_ASSERT=1 SFIZZ_ENABLE_RELEASE_DBG=1)
            set_target_properties(${target} PROPERTIES ${property} "${definitions}")
        endif()
    endforeach()
    target_compile_definitions(${target} PRIVATE NDEBUG)
endforeach()

# The app ships these CLAP plugins as their upstream releases build them. Dexed and Airwindows publish no Linux aarch64
# build, so there the app ships only Dragonfly. Dexed stays on 1.0.1 because the Linux build of 1.1.0 needs glibc 2.43.
# Airwindows replaces the files of its rolling release every week, so we download our own unchanged copies.
set(dragonfly_release
    https://github.com/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10)
set(dexed_release https://github.com/asb2m10/dexed/releases/download/v1.0.1/Dexed-1.0.1)
set(airwindows_release https://github.com/h4ks-com/emerick/releases/download/deps-airwindows-d013e10)
if(APPLE)
    FetchContent_Declare(dragonfly
        URL ${dragonfly_release}-macos-universal.dmg
        URL_HASH SHA256=e0b854b92a4e51ce5851320fb1a9f91ab1a4309f997de070e175b3e5cab4adaf
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_Declare(dexed
        URL ${dexed_release}-macOS.zip
        URL_HASH SHA256=4269448bc1312424cafd29aff615d891bc5905ebfc54b42ce70c56b9b10dc401)
    FetchContent_Declare(airwindows
        URL ${airwindows_release}/airwindows-consolidated-macOS-2026-10-31-d013e10.dmg
        URL_HASH SHA256=067106f1311f60ab6dc20d8cc92bbf9919de0136f299cccf0e53924223ec23eb
        DOWNLOAD_NO_EXTRACT TRUE)
    set(third_party_plugins dragonfly dexed airwindows)
elseif(WIN32)
    FetchContent_Declare(dragonfly
        URL ${dragonfly_release}-win64.zip
        URL_HASH SHA256=ca5f35f9dcd9a33f06490d3c92f04061a1a798cb81257d5ca5851bdda0824c66)
    FetchContent_Declare(dexed
        URL ${dexed_release}-win.zip
        URL_HASH SHA256=1f118445f2c5c411636bb41ddf8b456db09af74cd38f9ede8a93a01020505f05)
    FetchContent_Declare(airwindows
        URL ${airwindows_release}/AirwindowsConsolidated-2026-10-31-d013e10-Windows.zip
        URL_HASH SHA256=a21a0df8b0e47a96b4932fed3c05490c3f208fad84eb2fc73f4ca570dd34f292)
    set(third_party_plugins dragonfly dexed airwindows)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    FetchContent_Declare(dragonfly
        URL ${dragonfly_release}-linux-x86_64.tar.xz
        URL_HASH SHA256=59f1593120de82e31dd194e2ab1536af14fec2b3ec6ec45abf6d9c8ac02ef430)
    FetchContent_Declare(dexed
        URL ${dexed_release}-lnx.zip
        URL_HASH SHA256=2bac3d0c4237e8c22c4274b6d5b59fe6329fcae72864f81b5830393eff354fc1)
    FetchContent_Declare(airwindows
        URL ${airwindows_release}/AirwindowsConsolidated-2026-10-31-d013e10-Linux.zip
        URL_HASH SHA256=98f9f37788a14cfff97dad73332e7f7621e29bd2685b4ef5f0671273a1253ef4)
    set(third_party_plugins dragonfly dexed airwindows)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    FetchContent_Declare(dragonfly
        URL ${dragonfly_release}-linux-arm64.tar.xz
        URL_HASH SHA256=6c830ad1b1cc028b0f3c4f4c0a4b073d7193ba1c8783528ed9365df1fc9b23c2)
    set(third_party_plugins dragonfly)
endif()
FetchContent_MakeAvailable(${third_party_plugins})
# On macOS Dragonfly and Airwindows put their CLAP bundles in an installer package inside a disk image.
foreach(plugin IN LISTS third_party_plugins)
    set(folder ${${plugin}_SOURCE_DIR})
    file(GLOB disk_image ${folder}/*.dmg)
    if(disk_image AND NOT EXISTS ${folder}/installer)
        execute_process(COMMAND hdiutil attach -nobrowse -readonly -mountpoint ${folder}/mount ${disk_image}
            OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
        file(GLOB package ${folder}/mount/*.pkg)
        file(REMOVE_RECURSE ${folder}/partial)
        execute_process(COMMAND pkgutil --expand-full ${package} ${folder}/partial RESULT_VARIABLE expanded)
        execute_process(COMMAND hdiutil detach ${folder}/mount OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
        if(NOT expanded EQUAL 0)
            message(FATAL_ERROR "pkgutil could not expand ${package}")
        endif()
        file(RENAME ${folder}/partial ${folder}/installer)
    endif()
    file(GLOB claps LIST_DIRECTORIES true ${folder}/*.clap ${folder}/installer/*.pkg/Payload/*.clap)
    list(APPEND emerick_third_party_claps ${claps})
endforeach()
