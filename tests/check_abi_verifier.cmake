if(NOT DEFINED R_FRONT_EXECUTABLE OR
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
        OUTPUT_PREFIX "${OUTPUT_C}"
        ARGUMENTS_VARIABLE R_ABI_FRONT_ARGUMENTS
        INCLUDE_FLAGS ${R_ABI_INCLUDE_FLAGS})
endif()

# B9.1: r-front parses the verifier translation unit with libclang in the SDK and writes it only
# when every probe compiles; a disagreement is R-DIAG-FFI-004 on the R declaration, followed by
# what libclang reported.
execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    RESULT_VARIABLE R_SDK_PATH_RESULT
    OUTPUT_VARIABLE R_SDK_PATH
    ERROR_VARIABLE R_SDK_PATH_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT R_SDK_PATH_RESULT EQUAL 0 OR R_SDK_PATH STREQUAL "")
    message(FATAL_ERROR "could not resolve the macOS SDK path (${R_SDK_PATH_RESULT}):\n"
                        "${R_SDK_PATH_ERROR}")
endif()
list(APPEND R_ABI_FRONT_ARGUMENTS --sysroot "${R_SDK_PATH}")
if(DEFINED TARGET_MANIFEST AND NOT TARGET_MANIFEST STREQUAL "")
    list(APPEND R_ABI_FRONT_ARGUMENTS --target-manifest "${TARGET_MANIFEST}")
endif()
if(NOT (DEFINED ABI_RECORD AND NOT ABI_RECORD STREQUAL ""))
    foreach(R_ABI_DIR IN LISTS HEADER_INCLUDE_DIRS)
        list(APPEND R_ABI_FRONT_ARGUMENTS --abi-header-dir "${R_ABI_DIR}")
    endforeach()
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${R_ABI_FRONT_ARGUMENTS}
    RESULT_VARIABLE R_VERIFY_RESULT
    OUTPUT_FILE "${OUTPUT_C}"
    ERROR_VARIABLE R_VERIFY_ERROR
)
if(EXPECTATION STREQUAL "pass")
    if(NOT R_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "R-DIAG-FFI-004 [R-FFI-0042]: header verification rejected a correct import set:\n"
            "${R_VERIFY_ERROR}")
    endif()
elseif(EXPECTATION STREQUAL "fail")
    # The rejection shall come from the probe itself: an incompatible prototype or pointer
    # type, or a failed _Static_assert on a verified constant or object (R-CMAP-0019).
    if(NOT DEFINED FAIL_PATTERN OR FAIL_PATTERN STREQUAL "")
        set(FAIL_PATTERN "incompatible")
    endif()
    if(NOT R_VERIFY_RESULT EQUAL 1)
        message(FATAL_ERROR
            "header verification did not reject a mismatched import set (${R_VERIFY_RESULT}):\n"
            "${R_VERIFY_ERROR}")
    endif()
    if(NOT R_VERIFY_ERROR MATCHES "error R-DIAG-FFI-004 \\[R-(FFI|CMAP)-00[0-9][0-9]\\]: ")
        message(FATAL_ERROR
            "header verification did not report R-DIAG-FFI-004 on a declaration:\n${R_VERIFY_ERROR}")
    endif()
    if(NOT R_VERIFY_ERROR MATCHES "${FAIL_PATTERN}")
        message(FATAL_ERROR
            "header verification failed for an unexpected reason:\n${R_VERIFY_ERROR}")
    endif()
else()
    message(FATAL_ERROR "unknown ABI verifier expectation: ${EXPECTATION}")
endif()
