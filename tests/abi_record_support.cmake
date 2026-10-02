# Shared by the codegen, verifier and semantic test drivers: produces the ABI record of a
# fixture (R-FFI-0040/0041) by asking r-front for the inventory request and running
# tools/generate_c_abi_record.py with the target C compiler, then appends --abi-record to
# the frontend argument list held in the named variable.
#
# r_abi_generate_record(
#     FRONT <r-front>            PYTHON <python3>        TOOL <generate_c_abi_record.py>
#     CC <clang>                 OUTPUT_PREFIX <path>    ARGUMENTS_VARIABLE <list variable>
#     [INCLUDE_FLAGS <-Idir>...] [SYSROOT <path>])
function(r_abi_generate_record)
    set(R_ABI_OPTIONS)
    set(R_ABI_ONE FRONT PYTHON TOOL CC OUTPUT_PREFIX ARGUMENTS_VARIABLE SYSROOT)
    set(R_ABI_MANY INCLUDE_FLAGS)
    cmake_parse_arguments(R_ABI "${R_ABI_OPTIONS}" "${R_ABI_ONE}" "${R_ABI_MANY}" ${ARGN})
    if(NOT DEFINED R_ABI_FRONT OR NOT DEFINED R_ABI_PYTHON OR NOT DEFINED R_ABI_TOOL OR
       NOT DEFINED R_ABI_CC OR NOT DEFINED R_ABI_OUTPUT_PREFIX OR
       NOT DEFINED R_ABI_ARGUMENTS_VARIABLE)
        message(FATAL_ERROR "r_abi_generate_record: missing argument")
    endif()
    set(R_ABI_REQUEST "${R_ABI_OUTPUT_PREFIX}.abi-request.json")
    set(R_ABI_RECORD "${R_ABI_OUTPUT_PREFIX}.abi-record.json")
    set(R_ABI_REQUEST_ARGUMENTS ${${R_ABI_ARGUMENTS_VARIABLE}})
    # The first element is the --emit option of the caller.
    list(REMOVE_AT R_ABI_REQUEST_ARGUMENTS 0)
    execute_process(
        COMMAND "${R_ABI_FRONT}" --emit=abi-inventory ${R_ABI_REQUEST_ARGUMENTS}
        RESULT_VARIABLE R_ABI_REQUEST_RESULT
        OUTPUT_FILE "${R_ABI_REQUEST}"
        ERROR_VARIABLE R_ABI_REQUEST_ERROR
    )
    if(NOT R_ABI_REQUEST_RESULT EQUAL 0)
        message(FATAL_ERROR
            "r-front --emit=abi-inventory failed (${R_ABI_REQUEST_RESULT}): "
            "${R_ABI_REQUEST_ERROR}")
    endif()
    set(R_ABI_TOOL_ARGUMENTS
        "${R_ABI_TOOL}" --request "${R_ABI_REQUEST}" --cc "${R_ABI_CC}"
        --output "${R_ABI_RECORD}" ${R_ABI_INCLUDE_FLAGS})
    if(DEFINED R_ABI_SYSROOT AND NOT R_ABI_SYSROOT STREQUAL "")
        list(APPEND R_ABI_TOOL_ARGUMENTS --sysroot "${R_ABI_SYSROOT}")
    endif()
    execute_process(
        COMMAND "${R_ABI_PYTHON}" ${R_ABI_TOOL_ARGUMENTS}
        RESULT_VARIABLE R_ABI_TOOL_RESULT
        OUTPUT_VARIABLE R_ABI_TOOL_OUTPUT
        ERROR_VARIABLE R_ABI_TOOL_ERROR
    )
    if(NOT R_ABI_TOOL_RESULT EQUAL 0)
        message(FATAL_ERROR
            "ABI record generation failed (${R_ABI_TOOL_RESULT}):\n"
            "${R_ABI_TOOL_OUTPUT}${R_ABI_TOOL_ERROR}")
    endif()
    set(R_ABI_RESULT_ARGUMENTS ${${R_ABI_ARGUMENTS_VARIABLE}} --abi-record "${R_ABI_RECORD}")
    # R-FFI-0044: the frontend re-verifies the record's header digests against the same roots.
    foreach(R_ABI_INCLUDE_FLAG IN LISTS R_ABI_INCLUDE_FLAGS)
        string(REGEX REPLACE "^-I" "" R_ABI_HEADER_ROOT "${R_ABI_INCLUDE_FLAG}")
        list(APPEND R_ABI_RESULT_ARGUMENTS --abi-header-dir "${R_ABI_HEADER_ROOT}")
    endforeach()
    set(${R_ABI_ARGUMENTS_VARIABLE} ${R_ABI_RESULT_ARGUMENTS} PARENT_SCOPE)
endfunction()
