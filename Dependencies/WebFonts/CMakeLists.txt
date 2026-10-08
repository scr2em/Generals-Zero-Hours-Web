# Fonts for the WebAssembly build.
#
# The game draws its text through GDI (render2dsentence.cpp), which the WebCompat library implements
# with stb_truetype (Dependencies/WebCompat/src/win32_gdi.cpp). A GDI font is a face name that Windows
# resolves to an installed font; there are none in the browser, so the face names of the Windows fonts the
# game asks for are mapped to metric-compatible free fonts that are embedded into the program:
#
#   Arial            -> Liberation Sans
#   Times New Roman  -> Liberation Serif
#   Courier New      -> Liberation Mono
#
# Liberation Fonts 2.1.5 are licensed under the SIL Open Font License 1.1 (fonts/LICENSE-Liberation-OFL-1.1.txt,
# which must stay with the font files). The files are unmodified; the OFL reserves the name "Liberation"
# for unmodified versions, so do not subset or edit them without renaming.
#
# Size: the six files are 2.2 MB (about 1.1 MB compressed on the wire). Italic faces are not bundled; italics
# are synthesised. Remove entries from WEBFONTS_FILES to trade coverage for size; the lookup falls back to
# the faces that remain.
set(WEBFONTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fonts CACHE INTERNAL "Directory of the bundled fonts")
set(WEBFONTS_FILES
    LiberationSans-Regular.ttf
    LiberationSans-Bold.ttf
    LiberationSerif-Regular.ttf
    LiberationSerif-Bold.ttf
    LiberationMono-Regular.ttf
    LiberationMono-Bold.ttf
    CACHE INTERNAL "Bundled font files")

# Embeds the fonts into the program at /webfonts, where WebCompat looks for them
# (override with the WEBCOMPAT_FONT_DIR environment variable).
function(webfonts_embed target)
    foreach(font ${WEBFONTS_FILES})
        target_link_options(${target} PRIVATE "SHELL:--embed-file ${WEBFONTS_DIR}/${font}@/webfonts/${font}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${WEBFONTS_DIR}/${font})
    endforeach()
endfunction()
