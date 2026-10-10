# B8.1: a program built with --debug-info is debugged by its R source. The program is built and
# run as every other (check_codegen_program.cmake), then the debugger stops it at a line of the
# fixture and reads its frames and an owned string local through the DWARF of the object.
if(NOT DEFINED OUTPUT_EXE OR NOT DEFINED SOURCE_1 OR NOT DEFINED LLDB_EXECUTABLE)
    message(FATAL_ERROR "missing debug session test input")
endif()

set(FRONTEND_EXTRA_ARGUMENTS --debug-info --opt-level=0)
include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_program.cmake")

# The line after the declaration of `seven`, where it holds its text.
file(STRINGS "${SOURCE_1}" R_DEBUG_LINES)
set(R_DEBUG_LINE 0)
set(R_DEBUG_INDEX 0)
foreach(R_DEBUG_TEXT IN LISTS R_DEBUG_LINES)
    math(EXPR R_DEBUG_INDEX "${R_DEBUG_INDEX} + 1")
    if(R_DEBUG_TEXT MATCHES "std[.]string::string seven = ")
        math(EXPR R_DEBUG_LINE "${R_DEBUG_INDEX} + 1")
    endif()
endforeach()
if(R_DEBUG_LINE EQUAL 0)
    message(FATAL_ERROR "the fixture declares no local `seven`")
endif()
get_filename_component(R_DEBUG_FILE "${SOURCE_1}" NAME)

# The privacy protection of macOS keeps debugserver from launching a program stored in, or
# started from, the Desktop, Documents or Downloads folder: the launch waits for a permission that
# a batch session never gets (B8-5). The debugger therefore runs a copy in the temporary directory,
# started there, and reads the DWARF from the objects of the build tree, which the debug map of the
# copy names.
if(DEFINED ENV{TMPDIR} AND IS_DIRECTORY "$ENV{TMPDIR}")
    set(R_DEBUG_TEMPORARY "$ENV{TMPDIR}")
else()
    set(R_DEBUG_TEMPORARY "/tmp")
endif()
string(RANDOM LENGTH 12 R_DEBUG_SUFFIX)
set(R_DEBUG_DIRECTORY "${R_DEBUG_TEMPORARY}/r-debug-session-${R_DEBUG_SUFFIX}")
file(MAKE_DIRECTORY "${R_DEBUG_DIRECTORY}")
file(COPY "${OUTPUT_EXE}" DESTINATION "${R_DEBUG_DIRECTORY}")
get_filename_component(R_DEBUG_PROGRAM "${OUTPUT_EXE}" NAME)
execute_process(
    COMMAND "${LLDB_EXECUTABLE}" --no-lldbinit --batch
        -o "breakpoint set --file ${R_DEBUG_FILE} --line ${R_DEBUG_LINE}"
        -o "run"
        -o "bt 3"
        -o "frame variable seven"
        -k "quit"
        "${R_DEBUG_DIRECTORY}/${R_DEBUG_PROGRAM}"
    WORKING_DIRECTORY "${R_DEBUG_DIRECTORY}"
    RESULT_VARIABLE R_DEBUG_RESULT
    OUTPUT_VARIABLE R_DEBUG_OUTPUT
    ERROR_VARIABLE R_DEBUG_ERROR
    TIMEOUT 300)
file(REMOVE_RECURSE "${R_DEBUG_DIRECTORY}")
foreach(R_DEBUG_EXPECTED
        "stop reason = breakpoint"
        "test.codegen.dict_paths::owned at ${R_DEBUG_FILE}:${R_DEBUG_LINE}"
        "test.codegen.dict_paths::main at ${R_DEBUG_FILE}:"
        # The bytes are shown as a C string, which may run past the length.
        "seven = (data = \"key-7"
        ", length = 5)")
    string(FIND "${R_DEBUG_OUTPUT}" "${R_DEBUG_EXPECTED}" R_DEBUG_OFFSET)
    if(R_DEBUG_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "the debugger did not show '${R_DEBUG_EXPECTED}' (${R_DEBUG_RESULT}):\n"
            "${R_DEBUG_OUTPUT}${R_DEBUG_ERROR}")
    endif()
endforeach()
