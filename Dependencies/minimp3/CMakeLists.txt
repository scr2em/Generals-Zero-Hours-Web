# minimp3 (CC0), single header MP3 decoder. See README.md.
add_library(deps_minimp3 INTERFACE)
target_include_directories(deps_minimp3 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
