# web_net_test: not part of the normal build (ninja web_net_test). Two instances run in headless Chromium in
# one room of the signaling server, see run_net_test.mjs.
set(WEB_NET_CLIENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../../GeneralsMD/Code/Main/webnet/client")
add_executable(web_net_test EXCLUDE_FROM_ALL web_net_test.cpp)
target_link_libraries(web_net_test PRIVATE web_net_backend)
set_target_properties(web_net_test PROPERTIES SUFFIX ".html")
# The engine thread is a worker (PROXY_TO_PTHREAD) as in the game.
target_link_options(web_net_test PRIVATE
    -sPROXY_TO_PTHREAD=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sEXIT_RUNTIME=0
    -sPTHREAD_POOL_SIZE=2 -sASSERTIONS=1
    -sEXPORTED_RUNTIME_METHODS=addRunDependency,removeRunDependency
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html
)
set_property(TARGET web_net_test APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
add_custom_command(TARGET web_net_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WEB_NET_CLIENT_DIR}/zhnet.js $<TARGET_FILE_DIR:web_net_test>
    COMMENT "Copying the page's network module")
