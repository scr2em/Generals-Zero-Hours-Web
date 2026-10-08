# Direct3D 8 on WebGL2 (and D3DX8) for the WebAssembly build.
# See cmake/emscripten.cmake. dx8_SOURCE_DIR (the min-dx8-sdk headers) is
# defined by the including scope.

file(GLOB WEBD3D8_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(web_d3d8 STATIC ${WEBD3D8_SRC})

target_include_directories(web_d3d8
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${dx8_SOURCE_DIR}
)
target_link_libraries(web_d3d8 PUBLIC webcompat_headers)
target_compile_features(web_d3d8 PRIVATE cxx_std_20)
target_compile_definitions(web_d3d8 PRIVATE BUILD_WITH_D3D8)
target_compile_options(web_d3d8 PRIVATE
    -fshort-wchar -fms-extensions -fdeclspec -fno-strict-aliasing
    -Wno-microsoft -Wno-c++11-narrowing -Wno-register -Wno-deprecated-declarations
    -Wno-nonportable-include-path -Wno-ignored-attributes -Wno-unknown-pragmas
    -Wno-invalid-offsetof -Wno-inconsistent-missing-override -Wno-switch
)

# The WebGL2 backend (GLES3 headers + Emscripten's WebGL2 library).
target_link_options(web_d3d8 INTERFACE
    -sUSE_WEBGL2=1 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2
)

# Test program, not part of the normal build:
#   ninja -C build/em-d3d8 web_d3d8_test
# and see tests/README.md for running it in headless Chromium.
add_executable(web_d3d8_test EXCLUDE_FROM_ALL tests/web_d3d8_test.cpp)
target_include_directories(web_d3d8_test PRIVATE ${dx8_SOURCE_DIR})
target_compile_features(web_d3d8_test PRIVATE cxx_std_20)
target_compile_definitions(web_d3d8_test PRIVATE BUILD_WITH_D3D8)
target_compile_options(web_d3d8_test PRIVATE -fshort-wchar -fms-extensions -fdeclspec
    -Wno-microsoft -Wno-c++11-narrowing -Wno-register -Wno-deprecated-declarations
    -Wno-nonportable-include-path -Wno-ignored-attributes -Wno-unknown-pragmas)
target_link_libraries(web_d3d8_test PRIVATE web_d3d8)
set_target_properties(web_d3d8_test PROPERTIES SUFFIX ".html")
target_link_options(web_d3d8_test PRIVATE
    -sPROXY_TO_PTHREAD=1 -sOFFSCREENCANVAS_SUPPORT=1 -sJSPI=1 -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB -sEXIT_RUNTIME=0 -sASSERTIONS=1
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/shell.html
)
