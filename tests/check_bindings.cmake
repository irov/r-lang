# B9.2 (R-FFI-0009): r-front --emit=bindings fills the empty extern "C" block of STUB with the
# declarations of its headers. The output equals GOLDEN, and it is valid R that the same headers
# verify: its ABI record is derived from them (when the block names one) and every import,
# object, constant and C aggregate is proven against them by libclang (R-FFI-0042).
if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED STUB OR NOT DEFINED LINK_MANIFEST OR
   NOT DEFINED HEADER_DIR OR NOT DEFINED GOLDEN OR NOT DEFINED OUTPUT_PREFIX)
    message(FATAL_ERROR "missing bindings test input")
endif()

execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    RESULT_VARIABLE R_SDK_RESULT
    OUTPUT_VARIABLE R_SDK_PATH
    ERROR_VARIABLE R_SDK_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT R_SDK_RESULT EQUAL 0 OR R_SDK_PATH STREQUAL "")
    message(FATAL_ERROR "could not resolve the macOS SDK path (${R_SDK_RESULT}):\n${R_SDK_ERROR}")
endif()
set(R_BINDINGS_ARGUMENTS
    --link-manifest "${LINK_MANIFEST}" --sysroot "${R_SDK_PATH}" --abi-header-dir "${HEADER_DIR}")
set(R_BINDINGS_OUTPUT "${OUTPUT_PREFIX}.r")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=bindings ${R_BINDINGS_ARGUMENTS} "${STUB}"
    RESULT_VARIABLE R_BINDINGS_RESULT
    OUTPUT_FILE "${R_BINDINGS_OUTPUT}"
    ERROR_VARIABLE R_BINDINGS_ERROR
)
if(NOT R_BINDINGS_RESULT EQUAL 0)
    message(FATAL_ERROR "r-front --emit=bindings failed (${R_BINDINGS_RESULT}):\n${R_BINDINGS_ERROR}")
endif()
file(READ "${R_BINDINGS_OUTPUT}" R_BINDINGS_TEXT)
file(READ "${GOLDEN}" R_BINDINGS_GOLDEN)
if(NOT R_BINDINGS_TEXT STREQUAL R_BINDINGS_GOLDEN)
    message(FATAL_ERROR "the bindings of ${STUB} differ from ${GOLDEN}; compare ${R_BINDINGS_OUTPUT}")
endif()

set(R_BINDINGS_RECORD_ARGUMENTS)
file(STRINGS "${STUB}" R_BINDINGS_ABI REGEX "^@abi\\(")
if(R_BINDINGS_ABI)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=abi-record ${R_BINDINGS_ARGUMENTS} "${R_BINDINGS_OUTPUT}"
        RESULT_VARIABLE R_RECORD_RESULT
        OUTPUT_FILE "${OUTPUT_PREFIX}.record.json"
        ERROR_VARIABLE R_RECORD_ERROR
    )
    if(NOT R_RECORD_RESULT EQUAL 0)
        message(FATAL_ERROR "the ABI record of the bindings failed (${R_RECORD_RESULT}):\n${R_RECORD_ERROR}")
    endif()
    set(R_BINDINGS_RECORD_ARGUMENTS --abi-record "${OUTPUT_PREFIX}.record.json")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=abi-verifier ${R_BINDINGS_ARGUMENTS}
        ${R_BINDINGS_RECORD_ARGUMENTS} "${R_BINDINGS_OUTPUT}"
    RESULT_VARIABLE R_VERIFY_RESULT
    OUTPUT_FILE "${OUTPUT_PREFIX}.verifier.c"
    ERROR_VARIABLE R_VERIFY_ERROR
)
if(NOT R_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "the headers do not verify the bindings (${R_VERIFY_RESULT}):\n${R_VERIFY_ERROR}")
endif()
