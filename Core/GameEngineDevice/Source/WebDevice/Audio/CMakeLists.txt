# The browser sound device of the WebAssembly build: decoders (WAV/IMA ADPCM/MP3) and the Web
# Audio backend, as the static library web_audio_backend, and its tests.
#
# In the game build, add this directory with add_subdirectory(), link web_audio_backend to the
# game engine device library and add WebAudioManager.h/.cpp to that library's sources (they need
# the engine's include paths and precompiled header). Alternatively compile WebAudioDecoder.cpp and
# WebAudioBackend.cpp as part of that library too and add Dependencies/minimp3 to its include paths.
#
# Standalone build, for the tests:
#   source emsdk_env.sh
#   emcmake cmake -S Core/GameEngineDevice/Source/WebDevice/Audio -B build/em-audio-test -G Ninja
#   ninja -C build/em-audio-test web_audio_test
#   node Core/GameEngineDevice/Source/WebDevice/Audio/Tests/run_test.mjs build/em-audio-test/Tests
#   Tests/run_decoder_test.sh      (native decoder unit test with the sanitizers)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(web_audio LANGUAGES CXX)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "The web audio device is for Emscripten builds: use emcmake")
    endif()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # The same code generation options as the game (cmake/emscripten.cmake).
    string(APPEND CMAKE_CXX_FLAGS " -pthread -fwasm-exceptions")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -pthread -fwasm-exceptions")
endif()

get_filename_component(WEB_AUDIO_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

if(NOT TARGET deps_minimp3)
    add_subdirectory("${WEB_AUDIO_REPO_ROOT}/Dependencies/minimp3" "${CMAKE_CURRENT_BINARY_DIR}/minimp3")
endif()

add_library(web_audio_backend STATIC
    WebAudioBackend.cpp
    WebAudioDecoder.cpp
    ../../../Include/WebDevice/Audio/WebAudioBackend.h
    ../../../Include/WebDevice/Audio/WebAudioDecoder.h
    ../../../Include/WebDevice/Audio/WebAudioMix.h
)
target_include_directories(web_audio_backend PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../../Include")
target_link_libraries(web_audio_backend PRIVATE deps_minimp3)
target_compile_features(web_audio_backend PUBLIC cxx_std_17)

add_subdirectory(Tests)
