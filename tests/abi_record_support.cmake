# Shared by the codegen, verifier and semantic test drivers: produces the ABI record of a
# fixture (R-FFI-0040/0041) with r-front --emit=abi-record, which derives it from the fixture's
# headers with libclang for the target (B9.1), then appends --abi-record to the frontend argument
# list held in the named variable.
#
# r_abi_generate_record(
#     FRONT <r-front>            OUTPUT_PREFIX <path>    ARGUMENTS_VARIABLE <list variable>
#     [INCLUDE_FLAGS <-Idir>...] [SYSROOT <path>])
function(r_abi_generate_record)
    set(R_ABI_OPTIONS)
    set(R_ABI_ONE FRONT OUTPUT_PREFIX ARGUMENTS_VARIABLE SYSROOT)
    set(R_ABI_MANY INCLUDE_FLAGS)
    cmake_parse_arguments(R_ABI "${R_ABI_OPTIONS}" "${R_ABI_ONE}" "${R_ABI_MANY}" ${ARGN})
    if(NOT DEFINED R_ABI_FRONT OR NOT DEFINED R_ABI_OUTPUT_PREFIX OR
       NOT DEFINED R_ABI_ARGUMENTS_VARIABLE)
        message(FATAL_ERROR "r_abi_generate_record: missing argument")
    endif()
    set(R_ABI_RECORD "${R_ABI_OUTPUT_PREFIX}.abi-record.json")
    set(R_ABI_RECORD_ARGUMENTS ${${R_ABI_ARGUMENTS_VARIABLE}})
    # The first element is the --emit option of the caller.
    list(REMOVE_AT R_ABI_RECORD_ARGUMENTS 0)
    list(FIND R_ABI_RECORD_ARGUMENTS "--sysroot" R_ABI_SYSROOT_POSITION)
    if(R_ABI_SYSROOT_POSITION EQUAL -1)
        if(NOT DEFINED R_ABI_SYSROOT OR R_ABI_SYSROOT STREQUAL "")
            execute_process(
                COMMAND xcrun --sdk macosx --show-sdk-path
                RESULT_VARIABLE R_ABI_SDK_RESULT
                OUTPUT_VARIABLE R_ABI_SYSROOT
                ERROR_VARIABLE R_ABI_SDK_ERROR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(NOT R_ABI_SDK_RESULT EQUAL 0 OR R_ABI_SYSROOT STREQUAL "")
                message(FATAL_ERROR
                    "could not resolve the macOS SDK path (${R_ABI_SDK_RESULT}):\n${R_ABI_SDK_ERROR}")
            endif()
        endif()
        list(APPEND R_ABI_RECORD_ARGUMENTS --sysroot "${R_ABI_SYSROOT}")
    endif()
    foreach(R_ABI_INCLUDE_FLAG IN LISTS R_ABI_INCLUDE_FLAGS)
        string(REGEX REPLACE "^-I" "" R_ABI_HEADER_ROOT "${R_ABI_INCLUDE_FLAG}")
        list(APPEND R_ABI_RECORD_ARGUMENTS --abi-header-dir "${R_ABI_HEADER_ROOT}")
    endforeach()
    execute_process(
        COMMAND "${R_ABI_FRONT}" --emit=abi-record ${R_ABI_RECORD_ARGUMENTS}
        RESULT_VARIABLE R_ABI_RECORD_RESULT
        OUTPUT_FILE "${R_ABI_RECORD}"
        ERROR_VARIABLE R_ABI_RECORD_ERROR
    )
    if(NOT R_ABI_RECORD_RESULT EQUAL 0)
        message(FATAL_ERROR
            "r-front --emit=abi-record failed (${R_ABI_RECORD_RESULT}):\n${R_ABI_RECORD_ERROR}")
    endif()
    set(R_ABI_RESULT_ARGUMENTS ${${R_ABI_ARGUMENTS_VARIABLE}} --abi-record "${R_ABI_RECORD}")
    # R-FFI-0044: the frontend re-verifies the record's header digests against the same roots.
    foreach(R_ABI_INCLUDE_FLAG IN LISTS R_ABI_INCLUDE_FLAGS)
        string(REGEX REPLACE "^-I" "" R_ABI_HEADER_ROOT "${R_ABI_INCLUDE_FLAG}")
        list(APPEND R_ABI_RESULT_ARGUMENTS --abi-header-dir "${R_ABI_HEADER_ROOT}")
    endforeach()
    set(${R_ABI_ARGUMENTS_VARIABLE} ${R_ABI_RESULT_ARGUMENTS} PARENT_SCOPE)
endfunction()
