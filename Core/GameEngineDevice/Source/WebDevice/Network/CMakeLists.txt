# The engine thread's end of the browser network (WebNet.cpp, see GameNetwork/WebNet.h) as the static library
# web_net_backend, and its transport level test. The game build compiles WebNet.cpp as part of the game engine
# device library (Core/GameEngineDevice/CMakeLists.txt); this project is for the test only:
#
#   source emsdk_env.sh
#   emcmake cmake -S Core/GameEngineDevice/Source/WebDevice/Network -B build/em-nettest -G Ninja
#   ninja -C build/em-nettest web_net_test
#   node GeneralsMD/Code/Main/webnet/test/run_net_test.mjs build/em-nettest/Tests

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(web_net LANGUAGES CXX)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "The browser network is for Emscripten builds: use emcmake")
    endif()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # The same code generation options as the game (cmake/emscripten.cmake).
    string(APPEND CMAKE_CXX_FLAGS " -pthread -fwasm-exceptions")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -pthread -fwasm-exceptions")
endif()

add_library(web_net_backend STATIC
    WebNet.cpp
    ../../../Include/WebDevice/Network/WebNetShared.h
    ../../../../GameEngine/Include/GameNetwork/WebNet.h
)
target_include_directories(web_net_backend PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../../../Include"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../GameEngine/Include"
)
target_compile_features(web_net_backend PUBLIC cxx_std_17)

add_subdirectory(Tests)
