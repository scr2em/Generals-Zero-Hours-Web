# web_audio_test: not part of the normal build (ninja web_audio_test). Runs in headless
# Chromium, see run_test.mjs.
add_executable(web_audio_test EXCLUDE_FROM_ALL web_audio_test.cpp)
target_link_libraries(web_audio_test PRIVATE web_audio_backend)
set_target_properties(web_audio_test PROPERTIES SUFFIX ".html")
# The engine thread is a worker (PROXY_TO_PTHREAD) as in the game; three more pthreads are
# preallocated for the stream decoder and the browser's helpers.
target_link_options(web_audio_test PRIVATE
    -sPROXY_TO_PTHREAD=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sEXIT_RUNTIME=0
    -sPTHREAD_POOL_SIZE=3 -sASSERTIONS=1
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html
    --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/test_helpers.js
    --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data@/data
)
set_property(TARGET web_audio_test APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shell.html ${CMAKE_CURRENT_SOURCE_DIR}/test_helpers.js)
