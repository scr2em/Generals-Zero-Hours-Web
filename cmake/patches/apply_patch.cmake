# Applies a patch to the current working directory, doing nothing if it is
# already applied. Used as the PATCH_COMMAND of FetchContent_Declare, which may
# run again on a source directory that was patched before.
#
#   cmake -DPATCH_FILE=<file.patch> -DGIT_EXECUTABLE=<git> -P apply_patch.cmake

execute_process(
    COMMAND ${GIT_EXECUTABLE} apply --check --reverse "${PATCH_FILE}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
)

if(NOT already_applied EQUAL 0)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} apply "${PATCH_FILE}"
        RESULT_VARIABLE apply_result
    )
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply patch ${PATCH_FILE}")
    endif()
endif()
