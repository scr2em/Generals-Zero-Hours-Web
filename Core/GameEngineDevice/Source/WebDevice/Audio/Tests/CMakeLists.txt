# web_audio_test: not part of the normal build (ninja web_audio_test). Runs in headless
# Chromium, see run_test.mjs.
add_executable(web_audio_test EXCLUDE_FROM_ALL web_audio_test.cpp)
target_link_libraries(web_audio_test PRIVATE web_audio_backend)
set_target_properties(web_audio_test PROPERTIES SUFFIX ".html")
# The engine thread is a worker (PROXY_TO_PTHREAD) as in the game. The stream decoder thread is
# created on first use from that worker; WEB_AUDIO_TEST_POOL_SIZE is the number of workers started
# up front (1 is just the engine thread: then the decoder thread is created on demand).
set(WEB_AUDIO_TEST_POOL_SIZE 3 CACHE STRING "PTHREAD_POOL_SIZE of web_audio_test")
target_link_options(web_audio_test PRIVATE
    -sPROXY_TO_PTHREAD=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sEXIT_RUNTIME=0
    -sPTHREAD_POOL_SIZE=${WEB_AUDIO_TEST_POOL_SIZE} -sASSERTIONS=1
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html
    --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/test_helpers.js
    --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data@/data
)
set_property(TARGET web_audio_test APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shell.html ${CMAKE_CURRENT_SOURCE_DIR}/test_helpers.js)
