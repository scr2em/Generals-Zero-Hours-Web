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

# AddressSanitizer: every memory access is checked and the first bad write or read is reported with the stack of
# the code that did it (and of where the memory was allocated or freed). Slower and needs more memory; for finding
# memory corruption ("function signature mismatch" or crashes far from their cause). scripts/web/run.sh --asan.
option(RTS_WEB_ASAN "Build the WebAssembly game with AddressSanitizer" OFF)
if(RTS_WEB_ASAN)
    message(STATUS "Emscripten AddressSanitizer build (RTS_WEB_ASAN)")
    foreach(lang C CXX)
        string(APPEND CMAKE_${lang}_FLAGS " -fsanitize=address")
    endforeach()
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=address")
endif()

# Win32 treats any pointer below 64 KB as an integer resource id
# (IS_INTRESOURCE). Wasm places static data from address 1024, so string
# literals would look like resource ids. Start static data at 1 MB. (With ASan the
# shadow memory comes first, so the static data is far above 64 KB anyway; ASan
# does not allow a custom GLOBAL_BASE.)
if(NOT RTS_WEB_ASAN)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -sGLOBAL_BASE=1048576")
endif()

# Debug-friendly variant for finding runtime failures:
#   cmake --preset emscripten -B build/em-dbg -DRTS_WEB_DEBUG=ON -DRTS_DEBUG_LOGGING=ON
# Builds with -O1 and symbols (function names in browser stack traces), turns on
# the Emscripten runtime checks (ASSERTIONS, stack overflow detection) and, with
# RTS_WEB_SAFE_HEAP, checks every memory access for alignment and bounds (slow).
# The default build is unaffected.
option(RTS_WEB_DEBUG "Build the WebAssembly game with symbols, light optimization and runtime checks" OFF)
option(RTS_WEB_SAFE_HEAP "With RTS_WEB_DEBUG: also check every heap access (SAFE_HEAP, very slow)" OFF)
if(RTS_WEB_DEBUG)
    message(STATUS "Emscripten debug build (RTS_WEB_DEBUG)")
    foreach(lang C CXX)
        # Replace the per-configuration optimization flags; they come after
        # CMAKE_<LANG>_FLAGS on the command line, so appending -O1 there would lose.
        set(CMAKE_${lang}_FLAGS_RELEASE "-O1 -g -DNDEBUG")
        set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO "-O1 -g -DNDEBUG")
    endforeach()
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -g -sASSERTIONS=1 -sSTACK_OVERFLOW_CHECK=2 --pre-js ${CMAKE_CURRENT_LIST_DIR}/web_debug_prejs.js")
    if(RTS_WEB_SAFE_HEAP)
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -sSAFE_HEAP=1")
    endif()
else()
    # cmake/compilers.cmake adds -g to Release builds for crash analysis. With Emscripten that
    # makes the page download 75 MB of DWARF and stops the linker from running the full Binaryen
    # optimizer ("limited binaryen optimizations because DWARF info requested"). Keep only the
    # function names, which is what browser stack traces need, at a few MB.
    foreach(lang C CXX)
        string(REGEX REPLACE " -g( |$)" "\\1" CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
    endforeach()
    string(APPEND CMAKE_EXE_LINKER_FLAGS " --profiling-funcs")
endif()

# Every build: an engine thread that dies prints its stack to the page's log (see the file).
string(APPEND CMAKE_EXE_LINKER_FLAGS " --pre-js ${CMAKE_CURRENT_LIST_DIR}/web_crash_prejs.js")

add_subdirectory(Dependencies/WebCompat)
# The fonts that stand in for the Windows fonts (see Dependencies/WebFonts/CMakeLists.txt).
add_subdirectory(Dependencies/WebFonts)

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

# Direct3D 8 on WebGL2 (or WebGPU): dxWebGL2 (https://github.com/scr2em/dxWebGL2), which
# implements the interfaces the renderer uses (Core/GameEngineDevice, WW3D2) and
# provides the Direct3D 8 / D3DX 8 headers (<d3d8.h>, <d3dx8.h>).
#
# DXWEBGL2_GIT_TAG pins the version; to update, put the new commit (or tag) here.
# To develop against a local checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_DXWEBGL2=/path/to/dxWebGL2
set(DXWEBGL2_GIT_TAG 496cb7167efb4920e11497bcabc12835320ae5e1) # main: WebGPU (chosen when the browser offers it) and WebGL2
FetchContent_Declare(
    dxwebgl2
    GIT_REPOSITORY https://github.com/scr2em/dxWebGL2.git
    GIT_TAG        ${DXWEBGL2_GIT_TAG}
    EXCLUDE_FROM_ALL
)
set(DXWEBGL2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DXWEBGL2_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DXWEBGL2_INSTALL OFF CACHE BOOL "" FORCE)
# The game links with -pthread (PROXY_TO_PTHREAD): every object needs the atomics feature.
set(DXWEBGL2_BUILD_PTHREADS ON CACHE BOOL "" FORCE)
# Compile the WebGPU backend in (links Emscripten's emdawnwebgpu port; the game already links with -sJSPI, which
# it needs). WebGL2 stays the default; the page URL's ?arg=-dxwebgl2-backend=webgpu selects WebGPU, and the
# device falls back to WebGL2 (and says why in the log) when the browser has no WebGPU adapter.
set(DXWEBGL2_WEBGPU ON CACHE BOOL "" FORCE)
# Use WebCompat's windows.h / objbase.h and the game's flags (-fshort-wchar ...) through deps_config,
# so the library and the game agree on WCHAR and the Win32 base types.
set(DXWEBGL2_USE_PROJECT_WINDOWS_H ON CACHE BOOL "" FORCE)
set(DXWEBGL2_WINDOWS_H_TARGET deps_config CACHE STRING "" FORCE)
FetchContent_MakeAvailable(dxwebgl2)

# The rest of the DirectX 8 SDK (dinput.h, ddraw.h for the DDS file constants,
# dsound.h, ...) comes from min-dx8-sdk, unpatched. Its directories are searched
# only after the system, WebCompat and dxWebGL2 headers, so <d3d8.h> and <d3dx8.h>
# are dxWebGL2's and WebCompat's copies (basetsd.h) win over the SDK's.
FetchContent_Declare(
    dx8
    GIT_REPOSITORY https://github.com/TheSuperHackers/min-dx8-sdk.git
    GIT_TAG        7bddff8c01f5fb931c3cb73d4aa8e66d303d97bc
)
FetchContent_GetProperties(dx8)
if(NOT dx8_POPULATED)
    FetchContent_Populate(dx8)
endif()
add_library(d3d8lib INTERFACE)
target_compile_definitions(d3d8lib INTERFACE BUILD_WITH_D3D8)
target_compile_options(d3d8lib INTERFACE "SHELL:-idirafter ${dx8_SOURCE_DIR}" "SHELL:-idirafter ${dx8_SOURCE_DIR}/extra")
target_link_libraries(d3d8lib INTERFACE dxWebGL2::dxwebgl2_pthreads)
# The game reaches Direct3DCreate8 only through LoadLibrary("D3D8.DLL") / GetProcAddress, which
# WebCompat implements with a weak reference to dxwebgl2_LookupProc: make the linker take it.
target_link_options(d3d8lib INTERFACE "SHELL:-Wl,--undefined=dxwebgl2_LookupProc")

# The free starter content (original placeholder game data), generated next to
# the web page so the launcher can offer it. See Content/StarterPack/README.md.
add_subdirectory(Content)
