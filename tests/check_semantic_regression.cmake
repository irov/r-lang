if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE OR
   NOT DEFINED EXPECTATION)
    message(FATAL_ERROR "missing semantic regression input")
endif()

if(NOT DEFINED EMIT OR EMIT STREQUAL "")
    set(EMIT hir)
endif()
set(R_FRONT_COMMAND "${R_FRONT_EXECUTABLE}" "--emit=${EMIT}")
if(DEFINED TARGET_MANIFEST AND NOT TARGET_MANIFEST STREQUAL "")
    list(APPEND R_FRONT_COMMAND --target-manifest "${TARGET_MANIFEST}")
endif()
if(DEFINED PROFILE AND NOT PROFILE STREQUAL "")
    list(APPEND R_FRONT_COMMAND --profile "${PROFILE}")
endif()
if(DEFINED LIBRARY_MAP AND NOT LIBRARY_MAP STREQUAL "")
    list(APPEND R_FRONT_COMMAND --library-map "${LIBRARY_MAP}")
endif()
if(DEFINED DENY_PANIC_ALLOC AND DENY_PANIC_ALLOC)
    list(APPEND R_FRONT_COMMAND --deny-panic-alloc)
endif()
if(DEFINED LINK_MANIFEST AND NOT LINK_MANIFEST STREQUAL "")
    list(APPEND R_FRONT_COMMAND --link-manifest "${LINK_MANIFEST}")
endif()
if(DEFINED ENTRY AND NOT ENTRY STREQUAL "")
    list(APPEND R_FRONT_COMMAND --entry "${ENTRY}")
endif()
# R-FUNC-0025 (M24): translate the entry module in test mode.
if(DEFINED TEST_MODE AND TEST_MODE)
    list(APPEND R_FRONT_COMMAND --test)
endif()
list(APPEND R_FRONT_COMMAND "${SOURCE}")
if(DEFINED ABI_RECORD AND NOT ABI_RECORD STREQUAL "")
    list(APPEND R_FRONT_COMMAND --abi-record "${ABI_RECORD}")
    foreach(R_ABI_HEADER_DIR IN LISTS ABI_HEADER_DIRS)
        list(APPEND R_FRONT_COMMAND --abi-header-dir "${R_ABI_HEADER_DIR}")
    endforeach()
elseif(DEFINED ABI_INVENTORY AND ABI_INVENTORY)
    # The record is generated from the fixture's own headers; the first list element is
    # the executable, so the request arguments start after it.
    set(R_ABI_ARGUMENTS ${R_FRONT_COMMAND})
    list(REMOVE_AT R_ABI_ARGUMENTS 0)
    set(R_ABI_INCLUDE_FLAGS)
    foreach(R_ABI_DIR IN LISTS HEADER_INCLUDE_DIRS)
        list(APPEND R_ABI_INCLUDE_FLAGS "-I${R_ABI_DIR}")
    endforeach()
    include("${CMAKE_CURRENT_LIST_DIR}/abi_record_support.cmake")
    r_abi_generate_record(
        FRONT "${R_FRONT_EXECUTABLE}"
        PYTHON "${PYTHON_EXECUTABLE}"
        TOOL "${ABI_RECORD_TOOL}"
        CC "${C_COMPILER}"
        OUTPUT_PREFIX "${OUTPUT_PREFIX}"
        ARGUMENTS_VARIABLE R_ABI_ARGUMENTS
        INCLUDE_FLAGS ${R_ABI_INCLUDE_FLAGS})
    set(R_FRONT_COMMAND "${R_FRONT_EXECUTABLE}" ${R_ABI_ARGUMENTS})
endif()

execute_process(
    COMMAND ${R_FRONT_COMMAND}
    RESULT_VARIABLE R_FRONT_RESULT
    OUTPUT_VARIABLE R_FRONT_OUTPUT
    ERROR_VARIABLE R_FRONT_ERROR
)

if(EXPECTATION STREQUAL "accept")
    if(NOT R_FRONT_RESULT EQUAL 0)
        message(FATAL_ERROR "valid source was rejected: ${R_FRONT_ERROR}")
    endif()
    if(DEFINED REQUIRED_OUTPUT AND
       NOT REQUIRED_OUTPUT STREQUAL "" AND
       NOT R_FRONT_OUTPUT MATCHES "${REQUIRED_OUTPUT}")
        message(FATAL_ERROR "accepted source omitted required HIR: ${REQUIRED_OUTPUT}")
    endif()
elseif(EXPECTATION STREQUAL "reject")
    if(R_FRONT_RESULT EQUAL 0)
        message(FATAL_ERROR "invalid source was accepted")
    endif()
    if(DEFINED REQUIRED_DIAGNOSTIC AND
       NOT REQUIRED_DIAGNOSTIC STREQUAL "" AND
       NOT R_FRONT_ERROR MATCHES "${REQUIRED_DIAGNOSTIC}")
        message(FATAL_ERROR
            "rejected source omitted required diagnostic: ${REQUIRED_DIAGNOSTIC}\n"
            "${R_FRONT_ERROR}")
    endif()
    # M24-6: a diagnostic that only follows from the required one shall not be reported.
    if(DEFINED FORBIDDEN_DIAGNOSTIC AND
       NOT FORBIDDEN_DIAGNOSTIC STREQUAL "" AND
       R_FRONT_ERROR MATCHES "${FORBIDDEN_DIAGNOSTIC}")
        message(FATAL_ERROR
            "rejected source reported a cascaded diagnostic: ${FORBIDDEN_DIAGNOSTIC}\n"
            "${R_FRONT_ERROR}")
    endif()
else()
    message(FATAL_ERROR "unknown semantic regression expectation: ${EXPECTATION}")
endif()
