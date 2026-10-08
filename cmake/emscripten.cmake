# WebAssembly (Emscripten) build configuration.
#
# The game is built as a 32-bit "Windows-like" target: Dependencies/WebCompat
# provides the Win32 API, the MSVC C runtime extensions and the COM base
# interfaces on top of Emscripten's POSIX layer, so the game code keeps its
# Windows code paths. Configure with the "emscripten" preset:
#
#   emcmake cmake --preset emscripten
#   cmake --build build/emscripten

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "cmake/emscripten.cmake is only for Emscripten builds")
endif()

message(STATUS "Emscripten (WebAssembly) build")
set(IS_WEB_BUILD TRUE)

# The code base uses no C++20 modules; skip the per-file dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The engine runs its original blocking main loop on a worker thread
# (PROXY_TO_PTHREAD), and the game and GameSpy use threads, so every object
# file - including third-party dependencies - must be built with shared
# memory support. Set it globally so FetchContent dependencies get it too.
string(APPEND CMAKE_C_FLAGS " -pthread")
string(APPEND CMAKE_CXX_FLAGS " -pthread")
string(APPEND CMAKE_EXE_LINKER_FLAGS " -pthread")

# The engine relies on C++ exceptions (INI parsing, error recovery in the main
# loop). Emscripten disables catching by default; use native Wasm exceptions.
string(APPEND CMAKE_C_FLAGS " -fwasm-exceptions")
string(APPEND CMAKE_CXX_FLAGS " -fwasm-exceptions")
string(APPEND CMAKE_EXE_LINKER_FLAGS " -fwasm-exceptions")

# Win32 treats any pointer below 64 KB as an integer resource id
# (IS_INTRESOURCE). Wasm places static data from address 1024, so string
# literals would look like resource ids. Start static data at 1 MB.
string(APPEND CMAKE_EXE_LINKER_FLAGS " -sGLOBAL_BASE=1048576")

add_subdirectory(Dependencies/WebCompat)

# Flags for every game target.
target_compile_options(deps_config INTERFACE
    # The game stores UTF-16 text (WideChar) in files, replays, save games and
    # network packets, so wchar_t must be 16 bits as on Windows. The wide
    # character C library functions are reimplemented for 16-bit wchar_t in
    # Dependencies/WebCompat/src/wchar16.cpp.
    -fshort-wchar
    # __int64, __declspec, __forceinline and friends.
    -fms-extensions
    -fdeclspec
    # The code was written for x86 and freely type-puns.
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
)

target_link_libraries(deps_config INTERFACE webcompat_headers)

# DirectX 8 headers. The interfaces are implemented on WebGL2 by the
# web_d3d8 library (Core/GameEngineDevice, WebGL backend).
# The SDK headers pick the Windows layout (4 byte packing, the LARGE_INTEGER
# driver version, the interface ids) from _WIN32, which the web build does not
# define. The patch makes them accept __EMSCRIPTEN__ as well.
find_package(Git REQUIRED)
FetchContent_Declare(
    dx8
    GIT_REPOSITORY https://github.com/TheSuperHackers/min-dx8-sdk.git
    GIT_TAG        7bddff8c01f5fb931c3cb73d4aa8e66d303d97bc
    PATCH_COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/dx8-emscripten.patch
        -P ${CMAKE_CURRENT_LIST_DIR}/patches/apply_patch.cmake
)
FetchContent_GetProperties(dx8)
if(NOT dx8_POPULATED)
    FetchContent_Populate(dx8)
endif()
add_subdirectory(Dependencies/WebD3D8)
add_library(d3d8lib INTERFACE)
target_include_directories(d3d8lib INTERFACE ${dx8_SOURCE_DIR})
target_compile_definitions(d3d8lib INTERFACE BUILD_WITH_D3D8)
# ddraw.h (the DDS file constants), dsound.h and the d3dx math headers sit in
# the SDK's extra directory next to copies of headers that WebCompat provides
# (basetsd.h), so it must be searched last.
target_compile_options(d3d8lib INTERFACE "SHELL:-idirafter ${dx8_SOURCE_DIR}/extra")
if(TARGET web_d3d8)
    target_link_libraries(d3d8lib INTERFACE web_d3d8)
endif()
