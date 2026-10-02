if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED C_COMPILER OR
   NOT DEFINED SOURCE OR
   NOT DEFINED LINK_MANIFEST OR
   NOT DEFINED EXPECTATION OR
   NOT DEFINED OUTPUT_C)
    message(FATAL_ERROR "missing ABI verifier test input")
endif()

set(R_ABI_INCLUDE_FLAGS)
if(DEFINED HEADER_INCLUDE_DIRS AND NOT HEADER_INCLUDE_DIRS STREQUAL "")
    foreach(R_ABI_DIR IN LISTS HEADER_INCLUDE_DIRS)
        list(APPEND R_ABI_INCLUDE_FLAGS "-I${R_ABI_DIR}")
    endforeach()
endif()

set(R_ABI_FRONT_ARGUMENTS --emit=abi-verifier --link-manifest "${LINK_MANIFEST}" "${SOURCE}")
if(DEFINED ABI_RECORD AND NOT ABI_RECORD STREQUAL "")
    list(APPEND R_ABI_FRONT_ARGUMENTS --abi-record "${ABI_RECORD}")
    foreach(R_ABI_DIR IN LISTS HEADER_INCLUDE_DIRS)
        list(APPEND R_ABI_FRONT_ARGUMENTS --abi-header-dir "${R_ABI_DIR}")
    endforeach()
elseif(DEFINED ABI_INVENTORY AND ABI_INVENTORY)
    include("${CMAKE_CURRENT_LIST_DIR}/abi_record_support.cmake")
    r_abi_generate_record(
        FRONT "${R_FRONT_EXECUTABLE}"
        PYTHON "${PYTHON_EXECUTABLE}"
        TOOL "${ABI_RECORD_TOOL}"
        CC "${C_COMPILER}"
        OUTPUT_PREFIX "${OUTPUT_C}"
        ARGUMENTS_VARIABLE R_ABI_FRONT_ARGUMENTS
        INCLUDE_FLAGS ${R_ABI_INCLUDE_FLAGS})
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${R_ABI_FRONT_ARGUMENTS}
    RESULT_VARIABLE R_EMIT_RESULT
    OUTPUT_FILE "${OUTPUT_C}"
    ERROR_VARIABLE R_EMIT_ERROR
)
if(NOT R_EMIT_RESULT EQUAL 0)
    message(FATAL_ERROR "r-front --emit=abi-verifier failed (${R_EMIT_RESULT}): ${R_EMIT_ERROR}")
endif()

execute_process(
    COMMAND "${C_COMPILER}"
        -std=c17
        -pedantic-errors
        -Wall
        -Wextra
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes
        ${R_ABI_INCLUDE_FLAGS}
        -fsyntax-only
        "${OUTPUT_C}"
    RESULT_VARIABLE R_VERIFY_RESULT
    OUTPUT_VARIABLE R_VERIFY_OUTPUT
    ERROR_VARIABLE R_VERIFY_ERROR
)
if(EXPECTATION STREQUAL "pass")
    if(NOT R_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "R-DIAG-FFI-004 [R-FFI-0042]: header verification rejected a correct import set:\n"
            "${R_VERIFY_OUTPUT}${R_VERIFY_ERROR}")
    endif()
elseif(EXPECTATION STREQUAL "fail")
    # The rejection shall come from the probe itself: an incompatible prototype or pointer
    # type, or a failed _Static_assert on a verified constant or object (R-CMAP-0019).
    if(NOT DEFINED FAIL_PATTERN OR FAIL_PATTERN STREQUAL "")
        set(FAIL_PATTERN "incompatible")
    endif()
    if(R_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR "header verification accepted a mismatched import set")
    endif()
    if(NOT R_VERIFY_ERROR MATCHES "${FAIL_PATTERN}")
        message(FATAL_ERROR
            "header verification failed for an unexpected reason:\n${R_VERIFY_ERROR}")
    endif()
else()
    message(FATAL_ERROR "unknown ABI verifier expectation: ${EXPECTATION}")
endif()
