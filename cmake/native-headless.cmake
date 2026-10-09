# Native headless build: the game logic of the WebAssembly port as a command line program for Linux and
# macOS, without a window, renderer, audio or video. It exists to run the automated tests (-aiMatch, see
# scripts/aibench) without a browser.
#
# It is the web build compiled with the system compiler: the same Win32 stand-ins (Dependencies/WebCompat),
# the same engine (WebGameEngine, the std::filesystem file systems, the W3D devices in headless mode), with
# the browser parts replaced by native ones (Core/GameEngineDevice/Source/NativeDevice): a platform layer
# without input or canvas, the game files read in place from a directory, no-op audio and a Direct3D 8 that
# never creates a device (headless mode never asks for one). Configure with the "native-headless" preset:
#
#   cmake --preset native-headless
#   cmake --build build/native-headless --target zh_headless
#
# The program is 64-bit. See GeneralsMD/Code/Main/NativeMain.cpp for how to run it.

if(EMSCRIPTEN)
    message(FATAL_ERROR "cmake/native-headless.cmake is not for Emscripten builds")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The native headless build needs clang (Apple clang on macOS): the WebCompat layer uses "
        "clang's Microsoft extensions. Configure with CC=clang CXX=clang++.")
endif()

message(STATUS "Native headless build (${CMAKE_SYSTEM_NAME}, ${CMAKE_SYSTEM_PROCESSOR})")
# The game code is built exactly like for the web: the Win32 code paths on top of WebCompat.
set(IS_WEB_BUILD TRUE)
set(IS_NATIVE_HEADLESS_BUILD TRUE)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Release builds carry no debug information unless asked for (cmake/compilers.cmake adds -g): the objects
# are four times the size and the program is only for tests.
option(RTS_NATIVE_DEBUG_INFO "Native headless build: keep debug information in Release builds" OFF)
if(NOT RTS_NATIVE_DEBUG_INFO)
    foreach(lang C CXX)
        string(REGEX REPLACE " -g( |$)" "\\1" CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
    endforeach()
endif()

# The game's own zlib (1.1.4, cmake/zlib.cmake), as in the web build, not the system's: its zconf.h declares a Byte
# type that clashes with the game's.
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)

# The engine's static libraries refer to each other (the device layer implements functions that the engine calls).
# The linkers of Emscripten and macOS do not mind; GNU ld resolves libraries in one pass, so it gets all of them as
# one group that it searches until nothing new is found.
if(NOT APPLE)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--start-group")
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -Wl,--end-group")
endif()

find_package(Threads REQUIRED)
string(APPEND CMAKE_C_FLAGS " -pthread")
string(APPEND CMAKE_CXX_FLAGS " -pthread")

add_subdirectory(Dependencies/WebCompat)
target_link_libraries(webcompat PUBLIC Threads::Threads)

# Flags for every game target: the same as for the web build (see cmake/emscripten.cmake for the reasons).
target_compile_options(deps_config INTERFACE
    -fshort-wchar
    -fms-extensions
    -fdeclspec
    -fno-strict-aliasing
    -Wno-microsoft
    -Wno-c++11-narrowing
    -Wno-register
    -Wno-deprecated-declarations
    -Wno-nonportable-include-path
    -Wno-ignored-attributes
    -Wno-unknown-pragmas
    -Wno-unused-value
    -Wno-format
    -Wno-invalid-offsetof
    -Wno-inconsistent-missing-override
    -Wno-suggest-override
    -Wno-switch
    -Wno-implicit-exception-spec-mismatch
    # The game's memory manager (GameMemory.cpp) replaces operator new and hands out blocks that are 8-byte
    # aligned behind its 64-bit block headers; the compiler must not assume the 16 bytes of the x86-64 ABI
    # (it would store with aligned SSE instructions).
    -fnew-alignment=8
    # No fused multiply-add (arm64 has it, the WebAssembly build and x86-64 without -mfma do not): keeps the
    # floating point results of the platforms closer together.
    -ffp-contract=off
)
target_compile_definitions(deps_config INTERFACE ZH_NATIVE_HEADLESS=1)
if(APPLE)
    # The macOS SDK turns strlcpy and friends into checking macros, which breaks the game's own definitions
    # (Dependencies/Utility/Utility/stringex.h).
    target_compile_options(deps_config INTERFACE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
endif()
target_link_libraries(deps_config INTERFACE webcompat_headers)

# DirectX 8 headers, with the same patch as the web build. Headless mode never creates a device; the
# few functions the renderer links against come from Dependencies/NativeD3D8.
find_package(Git REQUIRED)
FetchContent_Declare(
    dx8
    GIT_REPOSITORY https://github.com/TheSuperHackers/min-dx8-sdk.git
    GIT_TAG        7bddff8c01f5fb931c3cb73d4aa8e66d303d97bc
    PATCH_COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/dx8-native.patch
        -P ${CMAKE_CURRENT_LIST_DIR}/patches/apply_patch.cmake
)
FetchContent_GetProperties(dx8)
if(NOT dx8_POPULATED)
    FetchContent_Populate(dx8)
endif()
add_subdirectory(Dependencies/NativeD3D8)
add_library(d3d8lib INTERFACE)
target_include_directories(d3d8lib INTERFACE ${dx8_SOURCE_DIR})
target_compile_definitions(d3d8lib INTERFACE BUILD_WITH_D3D8)
target_compile_options(d3d8lib INTERFACE "SHELL:-idirafter ${dx8_SOURCE_DIR}/extra")
target_link_libraries(d3d8lib INTERFACE native_d3d8)

# The free starter content, for quick runs without the real game data (Content/StarterPack/README.md).
add_subdirectory(Content)
