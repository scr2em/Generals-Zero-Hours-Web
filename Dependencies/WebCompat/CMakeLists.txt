# Win32 / MSVC runtime / COM stand-ins for the WebAssembly build.
# See cmake/emscripten.cmake.

# The headers, put ahead of the system include path so they stand in for the
# Windows SDK.
add_library(webcompat_headers INTERFACE)
target_include_directories(webcompat_headers BEFORE INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(APPLE)
    # Headers that macOS does not have (malloc.h), for the native headless build.
    target_include_directories(webcompat_headers BEFORE INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include/macos)
endif()
target_compile_options(webcompat_headers INTERFACE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/web_prelude.h"
)

# The implementations.
file(GLOB WEBCOMPAT_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(webcompat STATIC ${WEBCOMPAT_SRC})
target_link_libraries(webcompat PUBLIC webcompat_headers)
set_target_properties(webcompat PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
target_compile_options(webcompat PRIVATE -fshort-wchar -fms-extensions -fno-strict-aliasing)
target_link_libraries(webcompat_headers INTERFACE webcompat)
if(APPLE)
    # WebD3D8_LookupProc is a weak reference (src/win32_misc.cpp) that programs without the renderer leave
    # undefined. The macOS linker allows that only when it is told.
    target_link_options(webcompat INTERFACE "LINKER:-U,_WebD3D8_LookupProc")
endif()

# The GDI text emulation (gdi_font.cpp) rasterises with stb_truetype. The stb target is defined by
# cmake/stb.cmake, which the top level CMakeLists includes after cmake/emscripten.cmake; the name is
# resolved when the build system is generated.
target_link_libraries(webcompat PRIVATE stb)
