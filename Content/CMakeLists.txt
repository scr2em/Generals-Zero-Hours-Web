# Free starter content: original placeholder game data (one faction, one skirmish map) generated from source.
#
# Nothing in the pack is copied from Command & Conquer data; the generators in StarterPack/gen write every texture,
# model, sound, window layout and map from scratch, and the INI/string files under StarterPack/data are original text
# (GPL-3.0-or-later). The web launcher's "Play with free starter content" option downloads the result into the
# browser's file system.
#
# Wiring (web build only): cmake/emscripten.cmake contains
#
#     add_subdirectory(Content)
#
# which defines the target `starter_pack` (part of ALL when RTS_BUILD_STARTER_PACK is ON). The pack is built into
# <STARTER_PACK_SITE_DIR>/starterpack, next to z_generals.html, which is where the launcher fetches it from
# (importer.js: STARTER.baseUrl). Build just the pack with:
#
#     cmake --build <build dir> --target starter_pack
#
# Run the tests with:  python3 -m unittest discover -s Content/StarterPack/tools/tests   (about a minute)

option(RTS_BUILD_STARTER_PACK "Generate the free starter content next to the web page" ON)

if(NOT RTS_BUILD_STARTER_PACK)
    return()
endif()

find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_FOUND)
    message(WARNING "Python 3 not found: the free starter content will not be generated")
    return()
endif()

# Where the page lives. The game executable is placed in <build>/GeneralsMD.
set(STARTER_PACK_SITE_DIR "${CMAKE_BINARY_DIR}/GeneralsMD" CACHE PATH
    "Directory with z_generals.html; the starter pack is generated into its starterpack/ folder")
set(STARTER_PACK_DIR "${STARTER_PACK_SITE_DIR}/starterpack")
set(STARTER_PACK_STAMP "${CMAKE_CURRENT_BINARY_DIR}/starter_pack.stamp")

file(GLOB_RECURSE STARTER_PACK_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack/build_pack.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack/data/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack/gen/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack/tools/spk/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack/third_party/*")

add_custom_command(
    OUTPUT "${STARTER_PACK_STAMP}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack/build_pack.py"
            "${STARTER_PACK_SITE_DIR}" --name starterpack
    COMMAND "${CMAKE_COMMAND}" -E touch "${STARTER_PACK_STAMP}"
    DEPENDS ${STARTER_PACK_INPUTS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack"
    COMMENT "Generating the free starter content (original placeholder game data)"
    VERBATIM)

add_custom_target(starter_pack ALL DEPENDS "${STARTER_PACK_STAMP}")

# A pack that fails its own cross-reference checks should not ship; `ctest -R starter_pack` runs the checks when tests are enabled.
if(RTS_BUILD_OPTION_TESTS)
    add_test(NAME starter_pack_validate
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/StarterPack/tools/validate_pack.py" "${STARTER_PACK_DIR}"
        )
    set_tests_properties(starter_pack_validate PROPERTIES DEPENDS starter_pack)
endif()
