add_subdirectory(Precompiled)

add_library(depsi_always INTERFACE)
target_link_libraries(depsi_always INTERFACE
    deps_precompiled # Gives force included headers. Link early.
    deps_config
)

if (IS_VS6_BUILD)
    # The original max sdk does not compile against a modern compiler.
    # If there is a desire to make this work, then a fixed max sdk needs to be created.
    add_subdirectory(MaxSDK)
endif()

add_subdirectory(Utility)
add_subdirectory(Bink)
add_subdirectory(Miles)

if (WIN32)
    add_subdirectory(DbgHelp)
    add_subdirectory(Usp10)
elseif (IS_WEB_BUILD)
    # The game keeps its Windows code paths, which load dbghelp.dll through
    # the loader. It is never there on the web, so the loader does nothing.
    add_subdirectory(DbgHelp)
    add_library(deps_usp10 INTERFACE)
    # MP3 decoder of the Web Audio device
    add_subdirectory(minimp3)
    # FFmpeg (Bink demuxer and decoders) for the in-game videos (not in the native headless build)
    if(EMSCRIPTEN)
        add_subdirectory(FFmpegWeb)
    endif()
else()
    add_library(deps_dbghelp INTERFACE)
    add_library(deps_usp10 INTERFACE)
endif()
