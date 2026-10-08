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
    # "Read in place" mode (see webdirect/INTEGRATION.md): a WasmFS backend that serves the files of
    # the folder the player picked, without copying them into OPFS.
    set(ZH_DIRECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/webdirect)
    # The virtual LAN over WebRTC (webnet/README.md): the page's network module; the signaling server is in webnet/server.
    set(ZH_NET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/webnet)

    # The linker settings shared by the game and the platform test.
    #
    #  - The engine runs its blocking main loop on a worker thread
    #    (PROXY_TO_PTHREAD), the browser main thread only delivers input. WebGL is
    #    driven from that worker through an OffscreenCanvas transferred from the
    #    page's <canvas id="canvas"> (WebPlatform_GetCanvasSelector() returns the
    #    same selector). This needs a cross-origin isolated page, see web/serve.py.
    #  - The game data lives in the Origin Private File System, mounted through WasmFS, or is read in
    #    place from the player's folder (webdirect/); the user data always lives in OPFS.
    #  - The game keeps big buffers on the stack and a few hundred MB of data.
    function(zh_web_link_options target)
        target_link_options(${target} PRIVATE
            -sPROXY_TO_PTHREAD
            -sOFFSCREENCANVAS_SUPPORT
            "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
            -sWASMFS
            # JSPI (JavaScript Promise Integration, Chrome 137+): the engine thread's stack can
            # suspend, so the frame loop and the game's blocking loops that render from inside
            # themselves (load screens, fades, movies) can return to the browser for a frame without
            # being restructured, see WebPlatform_FramePresented(). Unlike ASYNCIFY it does not
            # rewrite the code (no size or speed cost); only the imports that wait (EM_ASYNC_JS) suspend.
            -sJSPI
            -sALLOW_MEMORY_GROWTH
            # Not GROWABLE_ARRAYBUFFERS: Chrome's WebGL rejects views of resizable/growable
            # buffers ("The provided ArrayBuffer value must not be resizable"), and WebD3D8
            # uploads straight from HEAPU8.
            -sGROWABLE_ARRAYBUFFERS=0
            -sINITIAL_MEMORY=512MB
            -sMAXIMUM_MEMORY=4GB
            -sSTACK_SIZE=8MB
            # Threads: the engine, the OPFS proxy worker of WasmFS, the audio stream decoder, and
            # spares for GameSpy / DNS / screenshot threads (each pool worker loads the module).
            -sPTHREAD_POOL_SIZE=6
            -sENVIRONMENT=web,worker
            -sEXIT_RUNTIME=0
            # The OPFS backend of WasmFS lists directories with stringToUTF8OnStack but does not
            # declare it as a dependency; without it every readdir fails with EIO (the game
            # finds its archives by listing the install directory).
            -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$stringToUTF8OnStack
            "SHELL:--shell-file ${ZH_WEB_DIR}/shell.html"
            # Read in place: hands the page's File objects to the engine thread (--pre-js) and serves
            # them through WasmFS (--js-library); the C++ side is WebStorage.cpp.
            "SHELL:--pre-js ${ZH_DIRECT_DIR}/zhdirect_pre.js"
            "SHELL:--js-library ${ZH_DIRECT_DIR}/library_zhdirect.js"
        )
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            ${ZH_WEB_DIR}/shell.html
            ${ZH_DIRECT_DIR}/zhdirect_pre.js
            ${ZH_DIRECT_DIR}/library_zhdirect.js)

        # The page and its scripts sit next to the build output.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ZH_WEB_DIR}/importer.js
                ${ZH_WEB_DIR}/coi-serviceworker.js
                ${ZH_WEB_DIR}/serve.py
                ${ZH_DIRECT_DIR}/direct-source.js
                ${ZH_NET_DIR}/client/zhnet.js
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying the web page scripts"
        )
    endfunction()

    zh_web_link_options(z_generals)
    # The fonts behind GDI text (Arial -> Liberation Sans, ...), at /webfonts.
    webfonts_embed(z_generals)

    # A tiny program that proves the plumbing in a browser without the game:
    # the engine thread, OPFS through WasmFS and the input message queue.
    #   cmake --build <dir> --target web_platform_test
    #   node web/test/e2e.mjs --site <dir>/GeneralsMD --fake <folder with ZeroHour and Generals>
    add_executable(web_platform_test EXCLUDE_FROM_ALL
        web/test/platform_test.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebKeyTable.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebPlatform.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebStorage.cpp
    )
    target_compile_features(web_platform_test PRIVATE cxx_std_20)
    target_include_directories(web_platform_test PRIVATE ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include)
    set_target_properties(web_platform_test PROPERTIES SUFFIX ".html")
    zh_web_link_options(web_platform_test)
    target_link_options(web_platform_test PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -lGL)

    # Test of the read in place mode without the game: the WasmFS backend, the file lookup, reads and
    # seeks, threads, the read cache. See webdirect/test/README.txt.
    #   cmake --build <dir> --target web_direct_test
    add_executable(web_direct_test EXCLUDE_FROM_ALL
        webdirect/test/direct_test.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebKeyTable.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebPlatform.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebStorage.cpp
    )
    target_compile_features(web_direct_test PRIVATE cxx_std_20)
    target_include_directories(web_direct_test PRIVATE ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include)
    target_link_libraries(web_direct_test PRIVATE deps_config)
    set_target_properties(web_direct_test PROPERTIES SUFFIX ".html")
    zh_web_link_options(web_direct_test)
    target_link_options(web_direct_test PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -lGL)
endif()

# File system test (OPFS mounts, Windows paths, case insensitivity, relative paths after
# chdir), see webtest/fs_test.cpp:
#   cmake --build <dir> --target web_fs_test
#   node webtest/smoke.mjs --site <dir>/GeneralsMD --page web_fs_test.html --data <data dir> --until FS_DONE
if(EMSCRIPTEN)
    add_executable(web_fs_test EXCLUDE_FROM_ALL
        webtest/fs_test.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebKeyTable.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebPlatform.cpp
        ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/WebDevice/Platform/WebStorage.cpp
    )
    target_compile_features(web_fs_test PRIVATE cxx_std_20)
    target_include_directories(web_fs_test PRIVATE ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include)
    target_link_libraries(web_fs_test PRIVATE deps_config)
    set_target_properties(web_fs_test PROPERTIES SUFFIX ".html")
    zh_web_link_options(web_fs_test)
    target_link_options(web_fs_test PRIVATE -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -lGL)
endif()
