# Direct3D 8 for the native headless build (cmake/native-headless.cmake): no device can be created, headless mode
# never creates one. The D3DX math (src/d3dx_math.cpp) is real (the game logic uses some of it), the rest are stubs.
add_library(native_d3d8 STATIC
    src/native_d3d8.cpp
    src/d3dx_math.cpp
)
target_include_directories(native_d3d8 PRIVATE ${dx8_SOURCE_DIR})
target_link_libraries(native_d3d8 PRIVATE deps_config)
target_compile_features(native_d3d8 PRIVATE cxx_std_20)
target_compile_definitions(native_d3d8 PRIVATE BUILD_WITH_D3D8)
target_compile_options(native_d3d8 PRIVATE "SHELL:-idirafter ${dx8_SOURCE_DIR}/extra")
