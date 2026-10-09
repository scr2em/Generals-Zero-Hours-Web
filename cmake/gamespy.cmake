set(GS_OPENSSL FALSE)
set(GAMESPY_SERVER_NAME "server.cnc-online.net")

if(EMSCRIPTEN)
    # The SDK picks its POSIX platform code (_UNIX, _LINUX, linux/*.c) from
    # __linux__, which Emscripten does not define. The patch makes it also
    # accept __EMSCRIPTEN__ in the public headers and in the platform source
    # selection, so the SDK build and the game code that includes the headers
    # agree on the platform without passing any definitions around.
    find_package(Git REQUIRED)
    set(GAMESPY_PATCH_COMMAND
        PATCH_COMMAND ${CMAKE_COMMAND}
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/gamespy-emscripten.patch
            -P ${CMAKE_CURRENT_LIST_DIR}/patches/apply_patch.cmake
    )
endif()

FetchContent_Declare(
    gamespy
    GIT_REPOSITORY https://github.com/TheSuperHackers/GamespySDK.git
    GIT_TAG        07e3d15c500415abc281efb74322ab6d9c857eb8
    ${GAMESPY_PATCH_COMMAND}
)

FetchContent_MakeAvailable(gamespy)
