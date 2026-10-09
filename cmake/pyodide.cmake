# Pyodide (CPython compiled to WebAssembly), the runtime of the in-browser army importer.
#
# The launcher's "Import armies from a mod" runs tools/zharmy (Python, standard library only) in a Web Worker under Pyodide.
# It is fetched at configure time from the npm registry's release tarball of the `pyodide` package (the same files as the
# release's "core" set: the runtime, the standard library and the package list, no extra Python packages) and checked
# against a fixed SHA-256. Only five files of it are used, see web/armyimport-worker.js.
#
#   -DRTS_WEB_PYODIDE=OFF   skips the download (the page then reports that this copy has no converter)
#   -DRTS_PYODIDE_DIR=<dir> uses an already unpacked `package/` directory (offline builds)
#
# Result: ZH_PYODIDE_DIR (the unpacked package), or empty when the converter is not part of the build.

option(RTS_WEB_PYODIDE "Ship Pyodide with the web page so the launcher can convert mods in the browser" ON)
set(RTS_PYODIDE_DIR "" CACHE PATH "An unpacked Pyodide package (offline builds); empty: download it")

set(ZH_PYODIDE_VERSION "0.29.5")
set(ZH_PYODIDE_URL "https://registry.npmjs.org/pyodide/-/pyodide-${ZH_PYODIDE_VERSION}.tgz")
set(ZH_PYODIDE_SHA256 "6749b10ee515a1458ecea36d860c8ce2a48366e85b3f92d749ad1840e4aff181")
# The files of the package the page needs (about 12 MB; 5.5 MB when the server compresses them).
set(ZH_PYODIDE_FILES pyodide.js pyodide.asm.js pyodide.asm.wasm python_stdlib.zip pyodide-lock.json)

set(ZH_PYODIDE_DIR "")
if(NOT RTS_WEB_PYODIDE)
    message(STATUS "Pyodide: off (the launcher will not be able to import armies from a mod)")
    return()
endif()

if(RTS_PYODIDE_DIR)
    set(_pyodide_root "${RTS_PYODIDE_DIR}")
else()
    set(_pyodide_download "${CMAKE_BINARY_DIR}/_deps/pyodide-${ZH_PYODIDE_VERSION}.tgz")
    set(_pyodide_root "${CMAKE_BINARY_DIR}/_deps/pyodide-${ZH_PYODIDE_VERSION}")
    if(NOT EXISTS "${_pyodide_root}/pyodide.asm.wasm")
        message(STATUS "Pyodide: downloading ${ZH_PYODIDE_URL}")
        file(DOWNLOAD "${ZH_PYODIDE_URL}" "${_pyodide_download}"
             EXPECTED_HASH SHA256=${ZH_PYODIDE_SHA256} STATUS _pyodide_status TLS_VERIFY ON)
        list(GET _pyodide_status 0 _pyodide_code)
        if(NOT _pyodide_code EQUAL 0)
            file(REMOVE "${_pyodide_download}")
            message(WARNING "Pyodide could not be downloaded (${_pyodide_status}); the launcher will not import armies "
                            "from a mod. Use -DRTS_PYODIDE_DIR=<unpacked package> or -DRTS_WEB_PYODIDE=OFF.")
            return()
        endif()
        file(REMOVE_RECURSE "${_pyodide_root}.tmp")
        file(MAKE_DIRECTORY "${_pyodide_root}.tmp")
        file(ARCHIVE_EXTRACT INPUT "${_pyodide_download}" DESTINATION "${_pyodide_root}.tmp")
        file(REMOVE_RECURSE "${_pyodide_root}")
        file(RENAME "${_pyodide_root}.tmp/package" "${_pyodide_root}")
        file(REMOVE_RECURSE "${_pyodide_root}.tmp")
        file(REMOVE "${_pyodide_download}")
    endif()
endif()

foreach(_f IN LISTS ZH_PYODIDE_FILES)
    if(NOT EXISTS "${_pyodide_root}/${_f}")
        message(WARNING "Pyodide: ${_pyodide_root}/${_f} is missing; the launcher will not import armies from a mod.")
        return()
    endif()
endforeach()
set(ZH_PYODIDE_DIR "${_pyodide_root}")
message(STATUS "Pyodide ${ZH_PYODIDE_VERSION}: ${ZH_PYODIDE_DIR}")
