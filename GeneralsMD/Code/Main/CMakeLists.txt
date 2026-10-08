if(EMSCRIPTEN)
    add_executable(z_generals)

    # z_generals.html (page), z_generals.js (loader), z_generals.wasm
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "z_generals" SUFFIX ".html")

    target_link_libraries(z_generals PRIVATE
        z_gameenginedevice
        zi_always
    )
else()
    add_executable(z_generals WIN32)

    # Use a binary name that doesn't conflict with original game.
    if("${CMAKE_SYSTEM}" MATCHES "Windows")
        set_target_properties(z_generals PROPERTIES OUTPUT_NAME "generalszh${RTS_BUILD_OUTPUT_SUFFIX}")
    else()
        set_target_properties(z_generals PROPERTIES OUTPUT_NAME "generalszh${RTS_BUILD_OUTPUT_SUFFIX}")
    endif()

    target_link_libraries(z_generals PRIVATE
        comctl32
        imm32
        vfw32
        winmm
        z_gameenginedevice
        zi_always
    )
endif()

# TODO Originally referred to build host and user, replace with git info perhaps?
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h
"#pragma once

#define VERSION_LOCALBUILDNUM 0
#define VERSION_BUILDUSER \"\"
#define VERSION_BUILDLOC \"\"
"
)

# Based on original binary values for these variables.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h
"#pragma once

#define VERSION_MAJOR 1
#define VERSION_MINOR 4
#define VERSION_BUILDNUM 601
"
)

if(MSVC)
    target_link_options(z_generals PRIVATE "/NODEFAULTLIB:libci.lib")
endif()

target_include_directories(z_generals PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(EMSCRIPTEN)
    # The web entry point replaces WinMain; resource.h and RTS.RC are Windows only.
    target_sources(z_generals PRIVATE
        WebMain.cpp
        WinMain.h
    )
else()
    target_sources(z_generals PRIVATE
        WinMain.cpp
        WinMain.h
    )
endif()

# RC files optional for MinGW builds
# Icon: LoadIcon() handles missing resource gracefully (uses default system icon)
# Manifest: DPI awareness metadata (nice to have but not essential)
if(MSVC)
    # VS2005 and later adds default manifest, we need to turn it off to prevent conflict with custom manifest
    if(NOT IS_VS6_BUILD)
        target_link_options(z_generals PRIVATE
            "/MANIFEST:NO"
        )
    endif()

    target_sources(z_generals PRIVATE
        RTS.RC
    )
endif()

# Strip debug symbols to separate file for MinGW Release builds
# This creates generalszh${RTS_BUILD_OUTPUT_SUFFIX}.exe.debug (similar to MSVC .pdb files)
if(MINGW AND COMMAND add_debug_strip_target)
    add_debug_strip_target(z_generals)
endif()

# WebAssembly: the page (shell), the engine thread and the browser storage.
if(EMSCRIPTEN)
    set(ZH_WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/web)

    # The linker settings shared by the game and the platform test.
    #
    #  - The engine runs its blocking main loop on a worker thread
    #    (PROXY_TO_PTHREAD), the browser main thread only delivers input. WebGL is
    #    driven from that worker through an OffscreenCanvas transferred from the
    #    page's <canvas id="canvas"> (WebPlatform_GetCanvasSelector() returns the
    #    same selector). This needs a cross-origin isolated page, see web/serve.py.
    #  - The game data lives in the Origin Private File System, mounted through WasmFS.
    #  - The game keeps big buffers on the stack and a few hundred MB of data.
    function(zh_web_link_options target)
        target_link_options(${target} PRIVATE
            -sPROXY_TO_PTHREAD
            -sOFFSCREENCANVAS_SUPPORT
            "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
            -sWASMFS
            -sALLOW_MEMORY_GROWTH
            -sGROWABLE_ARRAYBUFFERS=1
            -sINITIAL_MEMORY=512MB
            -sMAXIMUM_MEMORY=4GB
            -sSTACK_SIZE=8MB
            -sPTHREAD_POOL_SIZE=4
            -sENVIRONMENT=web,worker
            -sEXIT_RUNTIME=0
            "SHELL:--shell-file ${ZH_WEB_DIR}/shell.html"
        )
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${ZH_WEB_DIR}/shell.html)

        # The page and its scripts sit next to the build output.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ZH_WEB_DIR}/importer.js
                ${ZH_WEB_DIR}/coi-serviceworker.js
                ${ZH_WEB_DIR}/serve.py
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying the web page scripts"
        )
    endfunction()

    zh_web_link_options(z_generals)

    # A tiny program that proves the plumbing in a browser without the game:
    # the engine thread, OPFS through WasmFS and the input message queue.
    #   cmake --build <dir> --target web_platform_test
    #   node web/test/e2e.mjs --site <dir>/GeneralsMD/Code/Main --fake <folder with ZeroHour and Generals>
    add_executable(web_platform_test EXCLUDE_FROM_ALL
        web/test/platform_test.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebKeyTable.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebPlatform.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebStorage.cpp
    )
    target_include_directories(web_platform_test PRIVATE ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include)
    set_target_properties(web_platform_test PROPERTIES SUFFIX ".html")
    zh_web_link_options(web_platform_test)
    target_link_options(web_platform_test PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -lGL)
endif()
