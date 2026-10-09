# FFmpeg for the WebAssembly build: the game's videos (Bink 1, .bik) play in the browser.
#
# A minimal FFmpeg is built once per build directory with Emscripten, from a pinned release tag of
# the FFmpeg git repository (the sources are fetched and compiled when the target is first built, not
# when CMake configures). It contains only what the game's videos need:
#
#   libavformat  the Bink demuxer (BINK_AUD_* audio tracks included)
#   libavcodec   the Bink video decoder, the Bink audio decoders (RDFT and DCT)
#   libswscale   YUV -> RGB conversion of the frames
#   libavutil
#
# No network, no programs, no assembly, no encoders, filters, parsers, protocols or external libraries.
# The build is LGPL only (no --enable-gpl, no --enable-nonfree). The sources are the release as it is
# except for one two line patch (patches/avutil-tx-float-only.patch) that keeps 12.6 MB of unused
# transform tables out of the program. See NOTICE.md for the licensing notes and how to rebuild or
# replace the library.
#
# Options:
#   RTS_WEB_FFMPEG             ON: build FFmpeg and play videos (RTS_HAS_FFMPEG); OFF: no video playback.
#   RTS_WEB_FFMPEG_SOURCE_DIR  Use this FFmpeg checkout (the release below) instead of cloning, for
#                              builds without network access or to share one checkout. It is not patched
#                              (the program then holds 12.6 MB more of zeroed memory).
#
# The library is the INTERFACE target deps_ffmpeg_web (include directory, link order, build
# dependency); it does not exist when FFmpeg is disabled or cannot be built, so users test
# if(TARGET deps_ffmpeg_web).

option(RTS_WEB_FFMPEG "Build FFmpeg for the web build so that the game's Bink videos play" ON)
set(RTS_WEB_FFMPEG_SOURCE_DIR "" CACHE PATH "Existing FFmpeg checkout to build instead of cloning it")

if(NOT RTS_WEB_FFMPEG)
    message(STATUS "FFmpeg for the web build is disabled (RTS_WEB_FFMPEG=OFF): videos will not play")
    return()
endif()

include(ExternalProject)
include(ProcessorCount)

# The release is pinned by its tag; n7.1.1 is commit db69d06eeeab4f46da15030a80d539efb4503ca8.
set(FFMPEG_WEB_REPOSITORY "https://github.com/FFmpeg/FFmpeg.git")
set(FFMPEG_WEB_TAG "n7.1.1")

# emconfigure runs the configure script with the Emscripten compiler wrappers; the host programs it
# probes for are not used.
get_filename_component(_em_root "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(FFMPEG_WEB_EMCONFIGURE emconfigure HINTS "${_em_root}" "${EMSCRIPTEN_ROOT_PATH}")
find_program(FFMPEG_WEB_MAKE NAMES gmake make)
if(NOT FFMPEG_WEB_EMCONFIGURE OR NOT FFMPEG_WEB_MAKE)
    message(WARNING "FFmpeg for the web build needs emconfigure and make: videos will not play "
        "(emconfigure: ${FFMPEG_WEB_EMCONFIGURE}, make: ${FFMPEG_WEB_MAKE})")
    return()
endif()

# llvm-nm instead of the host nm (it has to read WebAssembly objects).
find_program(FFMPEG_WEB_NM llvm-nm HINTS "${_em_root}/../../bin" "${EMSCRIPTEN_ROOT_PATH}/../bin")
if(NOT FFMPEG_WEB_NM)
    set(FFMPEG_WEB_NM "${CMAKE_NM}")
endif()

ProcessorCount(_ffmpeg_jobs)
if(NOT _ffmpeg_jobs OR _ffmpeg_jobs EQUAL 0)
    set(_ffmpeg_jobs 4)
endif()

set(_ffmpeg_prefix "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-install")
set(_ffmpeg_build "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-build")
file(MAKE_DIRECTORY "${_ffmpeg_prefix}/include")

if(RTS_WEB_FFMPEG_SOURCE_DIR)
    set(_ffmpeg_source_args SOURCE_DIR "${RTS_WEB_FFMPEG_SOURCE_DIR}")
    set(_ffmpeg_patch_args "")
    set(_ffmpeg_download_args DOWNLOAD_COMMAND "")
else()
    set(_ffmpeg_source_args SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-src")
    # The one patch (see the patch file): applied to the clone only, never to a checkout of the user's.
    find_package(Git REQUIRED)
    set(_ffmpeg_patch_args PATCH_COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/avutil-tx-float-only.patch
        -P ${CMAKE_SOURCE_DIR}/cmake/patches/apply_patch.cmake)
    # UPDATE_DISCONNECTED: a tag in GIT_TAG would otherwise be fetched from the network on every build.
    set(_ffmpeg_download_args
        GIT_REPOSITORY "${FFMPEG_WEB_REPOSITORY}"
        GIT_TAG "${FFMPEG_WEB_TAG}"
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        UPDATE_DISCONNECTED TRUE)
endif()

# Everything of the game is built with -pthread (shared memory); objects without the atomics
# feature cannot be linked into it. FFmpeg's own threading is enabled (pthreads) so that its
# one-time table initialisation is thread safe; the decoders run single threaded.
set(_ffmpeg_configure
    "${FFMPEG_WEB_EMCONFIGURE}" <SOURCE_DIR>/configure
    --prefix=${_ffmpeg_prefix}
    --enable-cross-compile --target-os=none --arch=x86_32
    --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER}
    --ar=${CMAKE_AR} --ranlib=${CMAKE_RANLIB} --nm=${FFMPEG_WEB_NM}
    --disable-everything --disable-autodetect
    --disable-programs --disable-doc --disable-network --disable-debug
    --disable-asm --disable-x86asm --disable-inline-asm --disable-runtime-cpudetect
    --disable-iconv --disable-shared --enable-static --enable-small
    --disable-avdevice --disable-avfilter --disable-swresample --disable-postproc
    --enable-pthreads
    --enable-avformat --enable-avcodec --enable-swscale
    --enable-demuxer=bink
    --enable-decoder=bink,binkaudio_rdft,binkaudio_dct
    "--extra-cflags=-pthread -O2"
    "--extra-ldflags=-pthread")

set(_ffmpeg_libs
    "${_ffmpeg_prefix}/lib/libavformat.a"
    "${_ffmpeg_prefix}/lib/libavcodec.a"
    "${_ffmpeg_prefix}/lib/libswscale.a"
    "${_ffmpeg_prefix}/lib/libavutil.a")

ExternalProject_Add(ffmpeg_web_build
    ${_ffmpeg_source_args}
    ${_ffmpeg_download_args}
    ${_ffmpeg_patch_args}
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg"
    BINARY_DIR "${_ffmpeg_build}"
    CONFIGURE_COMMAND ${_ffmpeg_configure}
    BUILD_COMMAND "${FFMPEG_WEB_MAKE}" -j${_ffmpeg_jobs}
    INSTALL_COMMAND "${FFMPEG_WEB_MAKE}" install
    BUILD_BYPRODUCTS ${_ffmpeg_libs}
    USES_TERMINAL_DOWNLOAD TRUE
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    LOG_INSTALL TRUE
)

# Link order: libavformat needs libavcodec needs libswscale/libavutil.
add_library(deps_ffmpeg_web INTERFACE)
add_dependencies(deps_ffmpeg_web ffmpeg_web_build)
target_include_directories(deps_ffmpeg_web INTERFACE "${_ffmpeg_prefix}/include")
target_link_libraries(deps_ffmpeg_web INTERFACE ${_ffmpeg_libs})

message(STATUS "FFmpeg ${FFMPEG_WEB_TAG} for the web build: ${_ffmpeg_prefix}")

# A decoder test that runs under node, not part of the game build:
#   cmake --build <dir> --target ffmpeg_web_decode_test
#   python3 Dependencies/FFmpegWeb/test/run_decode_test.py \
#       --runner "node <dir>/Dependencies/FFmpegWeb/ffmpeg_web_decode_test.js"
# It generates Bink files with test/make_test_bik.py (our own tiny encoder, nothing copyrighted),
# decodes them with the library built above and checks frames, sizes and audio samples.
add_executable(ffmpeg_web_decode_test EXCLUDE_FROM_ALL test/bik_decode_test.c)
target_link_libraries(ffmpeg_web_decode_test PRIVATE deps_ffmpeg_web m)
target_link_options(ffmpeg_web_decode_test PRIVATE -sNODERAWFS=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1
    -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH)
set_target_properties(ffmpeg_web_decode_test PROPERTIES SUFFIX ".js")
