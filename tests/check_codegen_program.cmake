if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED C_COMPILER OR
   NOT DEFINED PYTHON_EXECUTABLE OR
   NOT DEFINED TARGET_TOOLCHAIN_CHECK OR
   NOT DEFINED TARGET_MANIFEST OR
   NOT DEFINED OUTPUT_C OR
   NOT DEFINED OUTPUT_EXE OR
   NOT DEFINED RUNTIME_INCLUDE OR
   NOT DEFINED RUNTIME_DARWIN_INCLUDE OR
   NOT DEFINED R_STD_ALLOC_INCLUDE OR
   NOT DEFINED R_STD_ASYNC_INCLUDE OR
   NOT DEFINED R_STD_C_INCLUDE OR
   NOT DEFINED R_STD_CONVERT_INCLUDE OR
   NOT DEFINED R_STD_ERROR_INCLUDE OR
   NOT DEFINED R_STD_STRING_INCLUDE OR
   NOT DEFINED R_LIBRARY_ROOT OR
   NOT DEFINED R_STD_ARC_LIBRARY OR
   NOT DEFINED R_STD_ALLOC_LIBRARY OR
   NOT DEFINED R_STD_ARRAY_LIBRARY OR
   NOT DEFINED R_STD_LIST_LIBRARY OR
   NOT DEFINED R_STD_DICT_LIBRARY OR
   NOT DEFINED R_STD_BITS_LIBRARY OR
   NOT DEFINED R_STD_BYTES_LIBRARY OR
   NOT DEFINED R_STD_HASH_LIBRARY OR
   NOT DEFINED R_STD_UTF8_LIBRARY OR
   NOT DEFINED R_CORE_LIBRARY OR
   NOT DEFINED R_STD_ASYNC_LIBRARY OR
   NOT DEFINED R_STD_C_LIBRARY OR
   NOT DEFINED R_STD_SECRET_LIBRARY OR
   NOT DEFINED R_STD_RANDOM_LIBRARY OR
   NOT DEFINED R_STD_SIGNAL_LIBRARY OR
   NOT DEFINED R_STD_TEST_LIBRARY OR
   NOT DEFINED R_STD_CONVERT_LIBRARY OR
   NOT DEFINED R_STD_FS_LIBRARY OR
   NOT DEFINED R_STD_IO_LIBRARY OR
   NOT DEFINED R_STD_RC_LIBRARY OR
   NOT DEFINED R_STD_SYNC_LIBRARY OR
   NOT DEFINED R_STD_THREAD_LIBRARY OR
   NOT DEFINED R_STD_TIME_LIBRARY OR
   NOT DEFINED RUNTIME_HOSTED_SOURCE OR
   NOT DEFINED RUNTIME_ALLOCATOR_SOURCE OR
   NOT DEFINED RUNTIME_ARRAY_SOURCE OR
   NOT DEFINED RUNTIME_LIST_SOURCE OR
   NOT DEFINED RUNTIME_DICT_SOURCE OR
   NOT DEFINED RUNTIME_ARC_SOURCE OR
   NOT DEFINED RUNTIME_RC_SOURCE OR
   NOT DEFINED RUNTIME_DROP_SOURCE OR
   NOT DEFINED RUNTIME_OWN_SOURCE OR
   NOT DEFINED RUNTIME_THREAD_ATTACHMENT_SOURCE OR
   NOT DEFINED RUNTIME_TASK_SOURCE OR
   NOT DEFINED RUNTIME_STACK_SOURCE OR
   NOT DEFINED RUNTIME_DARWIN_IO_LIBRARY OR
   NOT DEFINED RUNTIME_DARWIN_PROCESS_LIBRARY OR
   NOT DEFINED RUNTIME_DARWIN_EVENT_LIBRARY OR
   NOT DEFINED RUNTIME_DARWIN_TIMER_LIBRARY OR
   NOT DEFINED RUNTIME_FS_LANE_SOURCE OR
   NOT DEFINED RUNTIME_FS_LANE_ASYNC_SOURCE OR
   NOT DEFINED RUNTIME_FS_LANE_SYSCALLS_SOURCE OR
   NOT DEFINED RUNTIME_FS_SERVICE_SOURCE OR
   NOT DEFINED RUNTIME_STRING_SOURCE OR
   NOT DEFINED RUNTIME_UTF8_SOURCE OR
   NOT DEFINED RUNTIME_PANIC_SOURCE)
    message(FATAL_ERROR "missing codegen program test input")
endif()

if(NOT DEFINED GENERATED_C_OPTIMIZATION)
    set(GENERATED_C_OPTIMIZATION -O0)
endif()

execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${TARGET_TOOLCHAIN_CHECK}"
        --manifest "${TARGET_MANIFEST}"
        --cc "${C_COMPILER}"
    RESULT_VARIABLE R_TOOLCHAIN_RESULT
    OUTPUT_VARIABLE R_TOOLCHAIN_OUTPUT
    ERROR_VARIABLE R_TOOLCHAIN_ERROR
)
if(NOT R_TOOLCHAIN_RESULT EQUAL 0)
    message(FATAL_ERROR
        "target toolchain verification failed (${R_TOOLCHAIN_RESULT}):\n"
        "${R_TOOLCHAIN_OUTPUT}${R_TOOLCHAIN_ERROR}")
endif()

set(R_CODEGEN_PLATFORM_FLAGS)
if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        RESULT_VARIABLE R_SDK_PATH_RESULT
        OUTPUT_VARIABLE R_SDK_PATH
        ERROR_VARIABLE R_SDK_PATH_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT R_SDK_PATH_RESULT EQUAL 0 OR R_SDK_PATH STREQUAL "")
        message(FATAL_ERROR
            "could not resolve the macOS SDK path (${R_SDK_PATH_RESULT}):\n"
            "${R_SDK_PATH_ERROR}")
    endif()
    list(APPEND R_CODEGEN_PLATFORM_FLAGS -isysroot "${R_SDK_PATH}")
endif()

if((NOT DEFINED SOURCE_1 OR SOURCE_1 STREQUAL "") AND
   (NOT DEFINED MODULE_MAP OR MODULE_MAP STREQUAL "" OR
    NOT DEFINED ENTRY OR ENTRY STREQUAL ""))
    message(FATAL_ERROR "codegen program test requires sources or a module-map entry")
endif()

set(R_FRONTEND_ARGUMENTS --emit=c17)
if(DEFINED MODULE_MAP AND NOT MODULE_MAP STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --module-map "${MODULE_MAP}")
endif()
if(DEFINED ENTRY AND NOT ENTRY STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --entry "${ENTRY}")
endif()
if(DEFINED LIBRARY_MAP AND NOT LIBRARY_MAP STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --library-map "${LIBRARY_MAP}")
endif()
if(DEFINED PROFILE AND NOT PROFILE STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --profile "${PROFILE}")
endif()
if(DEFINED TARGET_MANIFEST AND NOT TARGET_MANIFEST STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --target-manifest "${TARGET_MANIFEST}")
endif()
if(DEFINED LINK_MANIFEST AND NOT LINK_MANIFEST STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS --link-manifest "${LINK_MANIFEST}")
endif()
if(DEFINED SOURCE_1 AND NOT SOURCE_1 STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS "${SOURCE_1}")
endif()
if(DEFINED SOURCE_2 AND NOT SOURCE_2 STREQUAL "")
    list(APPEND R_FRONTEND_ARGUMENTS "${SOURCE_2}")
endif()
# R-FUNC-0025 (M24): translate the entry module in test mode; appended last so that the
# argument positions above stay as they are.
if(TEST_MODE)
    list(APPEND R_FRONTEND_ARGUMENTS --test)
endif()

set(R_CODEGEN_SANITIZER_FLAGS)
if(DEFINED ENABLE_SANITIZERS AND ENABLE_SANITIZERS)
    list(APPEND R_CODEGEN_SANITIZER_FLAGS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
elseif(DEFINED ENABLE_THREAD_SANITIZER AND ENABLE_THREAD_SANITIZER)
    set(R_CODEGEN_SANITIZER_FLAGS
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
endif()

set(R_CODEGEN_COMPILE_SOURCE "${OUTPUT_C}")
set(R_CODEGEN_WRAPPER_FLAGS)
if(DEFINED C_WRAPPER AND NOT C_WRAPPER STREQUAL "")
    set(R_CODEGEN_COMPILE_SOURCE "${C_WRAPPER}")
    list(APPEND R_CODEGEN_WRAPPER_FLAGS
        "-DR_TEST_GENERATED_C=\"${OUTPUT_C}\"")
endif()
set(R_CODEGEN_LINK_INPUT "${R_CODEGEN_COMPILE_SOURCE}")
set(R_CODEGEN_LINK_GENERATED_FLAGS ${R_CODEGEN_WRAPPER_FLAGS})
set(R_CODEGEN_EXTRA_C_SOURCES)
if(DEFINED EXTRA_C_SOURCES AND NOT EXTRA_C_SOURCES STREQUAL "")
    set(R_CODEGEN_EXTRA_C_SOURCES ${EXTRA_C_SOURCES})
endif()
set(R_CODEGEN_HEADER_INCLUDE_FLAGS)
if(DEFINED HEADER_INCLUDE_DIRS AND NOT HEADER_INCLUDE_DIRS STREQUAL "")
    foreach(R_CODEGEN_HEADER_DIR IN LISTS HEADER_INCLUDE_DIRS)
        list(APPEND R_CODEGEN_HEADER_INCLUDE_FLAGS "-I${R_CODEGEN_HEADER_DIR}")
    endforeach()
endif()

if(DEFINED ABI_INVENTORY AND ABI_INVENTORY)
    # R-FFI-0041: complete C aggregates need the record produced from the verified headers.
    include("${CMAKE_CURRENT_LIST_DIR}/abi_record_support.cmake")
    r_abi_generate_record(
        FRONT "${R_FRONT_EXECUTABLE}"
        PYTHON "${PYTHON_EXECUTABLE}"
        TOOL "${ABI_RECORD_TOOL}"
        CC "${C_COMPILER}"
        OUTPUT_PREFIX "${OUTPUT_C}"
        ARGUMENTS_VARIABLE R_FRONTEND_ARGUMENTS
        INCLUDE_FLAGS ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
        SYSROOT "${R_SDK_PATH}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${R_FRONTEND_ARGUMENTS}
    RESULT_VARIABLE R_EMIT_RESULT
    OUTPUT_FILE "${OUTPUT_C}"
    ERROR_VARIABLE R_EMIT_ERROR
)
if(NOT R_EMIT_RESULT EQUAL 0)
    message(FATAL_ERROR
        "r-front --emit=c17 failed (${R_EMIT_RESULT}): ${R_EMIT_ERROR}")
endif()

file(READ "${OUTPUT_C}" R_GENERATED_C17)
if(HASH_ONLY)
    if(NOT R_GENERATED_C17 MATCHES "#include \"r_std_hash[.]h\"" OR
       R_GENERATED_C17 MATCHES "r_std_bytes_[a-z_]+[(]|r_runtime_array_[a-z_]+[(]")
        message(FATAL_ERROR "standalone hash program has incorrect module dependencies")
    endif()
    file(READ "${SOURCE_1}" R_HASH_SOURCE)
    string(REGEX MATCH "module ([A-Za-z0-9_.]+);" R_HASH_MODULE "${R_HASH_SOURCE}")
    set(R_HASH_PLAN_ARGUMENTS --entry "${CMAKE_MATCH_1}::main" ${R_FRONTEND_ARGUMENTS})
    list(REMOVE_AT R_HASH_PLAN_ARGUMENTS 2)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=link-plan ${R_HASH_PLAN_ARGUMENTS}
        RESULT_VARIABLE R_HASH_PLAN_RESULT
        OUTPUT_VARIABLE R_HASH_PLAN
        ERROR_VARIABLE R_HASH_PLAN_ERROR
    )
    if(NOT R_HASH_PLAN_RESULT EQUAL 0 OR
       NOT R_HASH_PLAN MATCHES "module=\"std[.]hash\" target=\"r_std_hash\"" OR
       R_HASH_PLAN MATCHES "std[.]bytes|r_std_bytes")
        message(FATAL_ERROR "standalone hash link plan: ${R_HASH_PLAN}${R_HASH_PLAN_ERROR}")
    endif()
    # The generated executable must link successfully with no bytes library on the command line.
    set(R_STD_BYTES_LIBRARY "")
endif()
if(R_GENERATED_C17 MATCHES "(^|[^A-Za-z0-9_])(setjmp|longjmp)[(]" OR
   R_GENERATED_C17 MATCHES "#[ \t]*include[ \t]*[<\"]setjmp[.]h[>\"]")
    message(FATAL_ERROR
        "generated C17 uses a forbidden setjmp/longjmp checked-error primitive")
endif()

# Library R-SLIB-RSRC-0003 (M25): a standard module written in R that imports the C functions of
# a native provider of the library, such as std.tls, is verified against the header of the
# provider and bridged like any extern "C" import (Core R-FFI-0042); the archives of the provider
# follow the generated code. With ABI_VERIFY the verifier and the bridge below cover these
# imports together with those of the program.
set(R_CODEGEN_STD_NATIVE_ARCHIVES)
set(R_CODEGEN_STD_NATIVE OFF)
if(R_GENERATED_C17 MATCHES "r_bridge_r_std_[a-z]+_native_")
    if(NOT DEFINED STD_NATIVE_INCLUDE_DIRS OR STD_NATIVE_INCLUDE_DIRS STREQUAL "")
        message(FATAL_ERROR "the program imports a native provider of the library without "
                            "STD_NATIVE_INCLUDE_DIRS")
    endif()
    foreach(R_STD_NATIVE_DIR IN LISTS STD_NATIVE_INCLUDE_DIRS)
        list(APPEND R_CODEGEN_HEADER_INCLUDE_FLAGS "-I${R_STD_NATIVE_DIR}")
    endforeach()
    set(R_CODEGEN_STD_NATIVE_ARCHIVES ${STD_NATIVE_LIBRARIES})
    set(R_CODEGEN_STD_NATIVE ON)
endif()

if(R_CODEGEN_STD_NATIVE AND NOT (DEFINED ABI_VERIFY AND ABI_VERIFY))
    set(R_STD_NATIVE_ARGUMENTS ${R_FRONTEND_ARGUMENTS})
    list(REMOVE_AT R_STD_NATIVE_ARGUMENTS 0)
    set(R_STD_NATIVE_VERIFIER_C "${OUTPUT_C}.native-verifier.c")
    set(R_STD_NATIVE_BRIDGE_C "${OUTPUT_C}.native-bridge.c")
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=abi-verifier ${R_STD_NATIVE_ARGUMENTS}
        RESULT_VARIABLE R_STD_NATIVE_EMIT_RESULT
        OUTPUT_FILE "${R_STD_NATIVE_VERIFIER_C}"
        ERROR_VARIABLE R_STD_NATIVE_EMIT_ERROR
    )
    if(NOT R_STD_NATIVE_EMIT_RESULT EQUAL 0)
        message(FATAL_ERROR "r-front --emit=abi-verifier failed (${R_STD_NATIVE_EMIT_RESULT}): "
                            "${R_STD_NATIVE_EMIT_ERROR}")
    endif()
    execute_process(
        COMMAND "${C_COMPILER}"
            ${R_CODEGEN_PLATFORM_FLAGS}
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
            ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
            -fsyntax-only
            "${R_STD_NATIVE_VERIFIER_C}"
        RESULT_VARIABLE R_STD_NATIVE_VERIFY_RESULT
        OUTPUT_VARIABLE R_STD_NATIVE_VERIFY_OUTPUT
        ERROR_VARIABLE R_STD_NATIVE_VERIFY_ERROR
    )
    if(NOT R_STD_NATIVE_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "R-DIAG-FFI-004 [R-FFI-0042]: header verification rejected the imports of a native "
            "provider of the library (${R_STD_NATIVE_VERIFY_RESULT}):\n"
            "${R_STD_NATIVE_VERIFY_OUTPUT}${R_STD_NATIVE_VERIFY_ERROR}")
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=c17-bridge ${R_STD_NATIVE_ARGUMENTS}
        RESULT_VARIABLE R_STD_NATIVE_BRIDGE_RESULT
        OUTPUT_FILE "${R_STD_NATIVE_BRIDGE_C}"
        ERROR_VARIABLE R_STD_NATIVE_BRIDGE_ERROR
    )
    if(NOT R_STD_NATIVE_BRIDGE_RESULT EQUAL 0)
        message(FATAL_ERROR "r-front --emit=c17-bridge failed (${R_STD_NATIVE_BRIDGE_RESULT}): "
                            "${R_STD_NATIVE_BRIDGE_ERROR}")
    endif()
    list(APPEND R_CODEGEN_EXTRA_C_SOURCES "${R_STD_NATIVE_BRIDGE_C}")
endif()

if(DEFINED ABI_VERIFY AND ABI_VERIFY)
    # R-FFI-0042: the verifier translation unit is compiled for the target with the generated-C
    # options and never executed; a failed compile is the R-DIAG-FFI-004 outcome.
    set(R_ABI_ARGUMENTS ${R_FRONTEND_ARGUMENTS})
    list(REMOVE_AT R_ABI_ARGUMENTS 0)
    set(R_ABI_VERIFIER_C "${OUTPUT_C}.abi-verifier.c")
    set(R_ABI_BRIDGE_C "${OUTPUT_C}.bridge.c")
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=abi-verifier ${R_ABI_ARGUMENTS}
        RESULT_VARIABLE R_ABI_EMIT_RESULT
        OUTPUT_FILE "${R_ABI_VERIFIER_C}"
        ERROR_VARIABLE R_ABI_EMIT_ERROR
    )
    if(NOT R_ABI_EMIT_RESULT EQUAL 0)
        message(FATAL_ERROR
            "r-front --emit=abi-verifier failed (${R_ABI_EMIT_RESULT}): ${R_ABI_EMIT_ERROR}")
    endif()
    execute_process(
        COMMAND "${C_COMPILER}"
            ${R_CODEGEN_PLATFORM_FLAGS}
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
            ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
            -fsyntax-only
            "${R_ABI_VERIFIER_C}"
        RESULT_VARIABLE R_ABI_VERIFY_RESULT
        OUTPUT_VARIABLE R_ABI_VERIFY_OUTPUT
        ERROR_VARIABLE R_ABI_VERIFY_ERROR
    )
    if(NOT R_ABI_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "R-DIAG-FFI-004 [R-FFI-0042]: header verification rejected the extern C "
            "imports (${R_ABI_VERIFY_RESULT}):\n${R_ABI_VERIFY_OUTPUT}${R_ABI_VERIFY_ERROR}")
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=c17-bridge ${R_ABI_ARGUMENTS}
        RESULT_VARIABLE R_ABI_BRIDGE_RESULT
        OUTPUT_FILE "${R_ABI_BRIDGE_C}"
        ERROR_VARIABLE R_ABI_BRIDGE_ERROR
    )
    if(NOT R_ABI_BRIDGE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "r-front --emit=c17-bridge failed (${R_ABI_BRIDGE_RESULT}): ${R_ABI_BRIDGE_ERROR}")
    endif()
    list(APPEND R_CODEGEN_EXTRA_C_SOURCES "${R_ABI_BRIDGE_C}")
endif()

if(DEFINED MEASURE_GENERATED_STACK_USAGE AND MEASURE_GENERATED_STACK_USAGE)
    file(READ "${TARGET_MANIFEST}" R_TARGET_MANIFEST_JSON_FOR_STACK)
    set(R_STACK_USAGE_OBJECT "${OUTPUT_C}.stack-usage.o")
    set(R_STACK_USAGE_REPORT "${OUTPUT_C}.stack-usage.su")
    set(R_STACK_USAGE_HEADER "${OUTPUT_C}.stack-usage.h")
    set(R_FINAL_STACK_USAGE_OBJECT "${OUTPUT_C}.final.o")
    set(R_FINAL_STACK_USAGE_REPORT "${OUTPUT_C}.final.su")
    set(R_STACK_USAGE_NONCONFORMING_MARKER
        "${OUTPUT_C}.stack-usage.nonconforming")
    file(REMOVE
        "${R_STACK_USAGE_OBJECT}"
        "${R_STACK_USAGE_REPORT}"
        "${R_STACK_USAGE_HEADER}"
        "${R_FINAL_STACK_USAGE_OBJECT}"
        "${R_FINAL_STACK_USAGE_REPORT}"
        "${R_STACK_USAGE_NONCONFORMING_MARKER}"
    )
    execute_process(
        COMMAND "${C_COMPILER}"
            ${R_CODEGEN_PLATFORM_FLAGS}
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
            -O0
            -c
            -fstack-usage
            -fno-inline-functions
            -fno-lto
            -DR_STACK_USAGE_MEASUREMENT=1
            ${R_CODEGEN_WRAPPER_FLAGS}
            "-I${RUNTIME_INCLUDE}"
            "-I${RUNTIME_DARWIN_INCLUDE}"
            "-I${R_LIBRARY_ROOT}/core/include"
            "-I${R_STD_ALLOC_INCLUDE}"
            "-I${R_LIBRARY_ROOT}/std/arc/include"
            "-I${R_LIBRARY_ROOT}/std/array/include"
            "-I${R_STD_ASYNC_INCLUDE}"
            "-I${R_LIBRARY_ROOT}/std/bits/include"
            "-I${R_LIBRARY_ROOT}/std/bytes/include"
            "-I${R_LIBRARY_ROOT}/std/hash/include"
            "-I${R_LIBRARY_ROOT}/std/secret/include"
            "-I${R_LIBRARY_ROOT}/std/random/include"
            "-I${R_LIBRARY_ROOT}/std/signal/include"
            "-I${R_LIBRARY_ROOT}/std/test/include"
            "-I${R_STD_C_INCLUDE}"
            "-I${R_STD_CONVERT_INCLUDE}"
            "-I${R_LIBRARY_ROOT}/std/dict/include"
            "-I${R_LIBRARY_ROOT}/std/env/include"
            "-I${R_STD_ERROR_INCLUDE}"
            "-I${R_LIBRARY_ROOT}/std/format/include"
            "-I${R_LIBRARY_ROOT}/std/json/include"
            "-I${R_LIBRARY_ROOT}/std/fs/include"
            "-I${R_LIBRARY_ROOT}/std/io/include"
            "-I${R_LIBRARY_ROOT}/std/list/include"
            "-I${R_LIBRARY_ROOT}/std/math/include"
            "-I${R_LIBRARY_ROOT}/std/net/include"
            "-I${R_LIBRARY_ROOT}/std/process/include"
            "-I${R_LIBRARY_ROOT}/std/rc/include"
            "-I${R_STD_STRING_INCLUDE}"
            "-I${R_LIBRARY_ROOT}/std/sync/include"
            "-I${R_LIBRARY_ROOT}/std/thread/include"
            "-I${R_LIBRARY_ROOT}/std/time/include"
            "-I${R_LIBRARY_ROOT}/std/utf8/include"
            "${R_CODEGEN_COMPILE_SOURCE}"
            -o "${R_STACK_USAGE_OBJECT}"
        RESULT_VARIABLE R_STACK_USAGE_RESULT
        OUTPUT_VARIABLE R_STACK_USAGE_OUTPUT
        ERROR_VARIABLE R_STACK_USAGE_ERROR
    )
    if(NOT R_STACK_USAGE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "generated C17 stack-usage compile failed "
            "(${R_STACK_USAGE_RESULT}):\n"
            "${R_STACK_USAGE_OUTPUT}${R_STACK_USAGE_ERROR}")
    endif()
    set(STACK_USAGE_OBJECT "${R_STACK_USAGE_OBJECT}")
    set(STACK_USAGE_REPORT "${R_STACK_USAGE_REPORT}")
    set(STACK_USAGE_SOURCE "${OUTPUT_C}")
    set(STACK_USAGE_BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set(STACK_USAGE_HEADER "${R_STACK_USAGE_HEADER}")
    set(STACK_USAGE_MODE "generate")
    string(JSON R_STACK_USAGE_ENTRY_BUDGET
        ERROR_VARIABLE R_STACK_USAGE_BUDGET_ERROR
        GET "${R_TARGET_MANIFEST_JSON_FOR_STACK}"
        core stack entry_budget_bytes)
    if(NOT R_STACK_USAGE_BUDGET_ERROR STREQUAL "NOTFOUND" OR
       NOT R_STACK_USAGE_ENTRY_BUDGET MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "target manifest has no valid entry stack budget: ${R_STACK_USAGE_BUDGET_ERROR}")
    endif()
    set(STACK_USAGE_PYTHON "${PYTHON_EXECUTABLE}")
    set(STACK_USAGE_ENTRY_TOOL "${CMAKE_CURRENT_LIST_DIR}/../tools/compute_stack_entries.py")
    set(STACK_USAGE_ENTRY_BUDGET "${R_STACK_USAGE_ENTRY_BUDGET}")
    include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_stack_usage.cmake")

    set(R_STACK_USAGE_INSTRUMENTED FALSE)
    if((DEFINED ENABLE_SANITIZERS AND ENABLE_SANITIZERS) OR
       (DEFINED ENABLE_THREAD_SANITIZER AND ENABLE_THREAD_SANITIZER))
        set(R_STACK_USAGE_INSTRUMENTED TRUE)
    endif()

    file(READ "${TARGET_MANIFEST}" R_TARGET_MANIFEST_JSON)
    string(JSON R_STACK_USAGE_MAX_CANDIDATE_ITERATIONS
        ERROR_VARIABLE R_STACK_USAGE_ITERATION_ERROR
        GET "${R_TARGET_MANIFEST_JSON}"
        core stack frame_measurement artifact_pipeline fixed_point
        maximum_candidate_iterations)
    if(NOT R_STACK_USAGE_ITERATION_ERROR STREQUAL "NOTFOUND" OR
       NOT R_STACK_USAGE_MAX_CANDIDATE_ITERATIONS MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "target manifest has no valid stack fixed-point iteration limit: "
            "${R_STACK_USAGE_ITERATION_ERROR}")
    endif()

    set(R_STACK_USAGE_CONVERGED FALSE)
    set(R_STACK_USAGE_ITERATION 0)
    while(NOT R_STACK_USAGE_CONVERGED)
        math(EXPR R_STACK_USAGE_ITERATION "${R_STACK_USAGE_ITERATION} + 1")
        if(R_STACK_USAGE_ITERATION GREATER R_STACK_USAGE_MAX_CANDIDATE_ITERATIONS)
            message(FATAL_ERROR
                "generated stack bounds did not reach a fixed point after "
                "${R_STACK_USAGE_MAX_CANDIDATE_ITERATIONS} candidate compiles")
        endif()
        execute_process(
            COMMAND "${C_COMPILER}"
                ${R_CODEGEN_PLATFORM_FLAGS}
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
                -O0
                -c
                -fstack-usage
                -fno-inline-functions
                -fno-lto
                -include "${R_STACK_USAGE_HEADER}"
                ${R_CODEGEN_SANITIZER_FLAGS}
                ${R_CODEGEN_WRAPPER_FLAGS}
                "-I${RUNTIME_INCLUDE}"
                "-I${RUNTIME_DARWIN_INCLUDE}"
                "-I${R_LIBRARY_ROOT}/core/include"
                "-I${R_STD_ALLOC_INCLUDE}"
                "-I${R_LIBRARY_ROOT}/std/arc/include"
                "-I${R_LIBRARY_ROOT}/std/array/include"
                "-I${R_STD_ASYNC_INCLUDE}"
                "-I${R_LIBRARY_ROOT}/std/bits/include"
                "-I${R_LIBRARY_ROOT}/std/bytes/include"
                "-I${R_LIBRARY_ROOT}/std/hash/include"
                "-I${R_LIBRARY_ROOT}/std/secret/include"
                "-I${R_LIBRARY_ROOT}/std/random/include"
                "-I${R_LIBRARY_ROOT}/std/signal/include"
                "-I${R_LIBRARY_ROOT}/std/test/include"
                "-I${R_STD_C_INCLUDE}"
                "-I${R_STD_CONVERT_INCLUDE}"
                "-I${R_LIBRARY_ROOT}/std/dict/include"
                "-I${R_LIBRARY_ROOT}/std/env/include"
                "-I${R_STD_ERROR_INCLUDE}"
                "-I${R_LIBRARY_ROOT}/std/format/include"
            "-I${R_LIBRARY_ROOT}/std/json/include"
                "-I${R_LIBRARY_ROOT}/std/fs/include"
                "-I${R_LIBRARY_ROOT}/std/io/include"
                "-I${R_LIBRARY_ROOT}/std/list/include"
                "-I${R_LIBRARY_ROOT}/std/math/include"
                "-I${R_LIBRARY_ROOT}/std/net/include"
                "-I${R_LIBRARY_ROOT}/std/process/include"
                "-I${R_LIBRARY_ROOT}/std/rc/include"
                "-I${R_STD_STRING_INCLUDE}"
                "-I${R_LIBRARY_ROOT}/std/sync/include"
                "-I${R_LIBRARY_ROOT}/std/thread/include"
                "-I${R_LIBRARY_ROOT}/std/time/include"
                "-I${R_LIBRARY_ROOT}/std/utf8/include"
                "${R_CODEGEN_COMPILE_SOURCE}"
                -o "${R_FINAL_STACK_USAGE_OBJECT}"
            RESULT_VARIABLE R_FINAL_STACK_USAGE_RESULT
            OUTPUT_VARIABLE R_FINAL_STACK_USAGE_OUTPUT
            ERROR_VARIABLE R_FINAL_STACK_USAGE_ERROR
        )
        if(NOT R_FINAL_STACK_USAGE_RESULT EQUAL 0)
            message(FATAL_ERROR
                "generated C17 stack candidate ${R_STACK_USAGE_ITERATION} failed "
                "(${R_FINAL_STACK_USAGE_RESULT}):\n"
                "${R_FINAL_STACK_USAGE_OUTPUT}${R_FINAL_STACK_USAGE_ERROR}")
        endif()
        if(NOT EXISTS "${R_FINAL_STACK_USAGE_OBJECT}" OR
           IS_DIRECTORY "${R_FINAL_STACK_USAGE_OBJECT}")
            message(FATAL_ERROR
                "generated C17 stack candidate did not produce an object: "
                "${R_FINAL_STACK_USAGE_OBJECT}")
        endif()
        file(SIZE "${R_FINAL_STACK_USAGE_OBJECT}" R_FINAL_STACK_USAGE_OBJECT_SIZE)
        if(R_FINAL_STACK_USAGE_OBJECT_SIZE EQUAL 0)
            message(FATAL_ERROR
                "generated C17 stack candidate produced an empty object: "
                "${R_FINAL_STACK_USAGE_OBJECT}")
        endif()

        if(R_STACK_USAGE_INSTRUMENTED)
            set(R_STACK_USAGE_CONVERGED TRUE)
        else()
            file(READ "${R_STACK_USAGE_HEADER}" R_STACK_USAGE_HEADER_BEFORE_MERGE)
            set(STACK_USAGE_OBJECT "${R_FINAL_STACK_USAGE_OBJECT}")
            set(STACK_USAGE_REPORT "${R_FINAL_STACK_USAGE_REPORT}")
            set(STACK_USAGE_MODE "merge")
            include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_stack_usage.cmake")
            file(READ "${R_STACK_USAGE_HEADER}" R_STACK_USAGE_HEADER_AFTER_MERGE)
            if(R_STACK_USAGE_HEADER_AFTER_MERGE STREQUAL R_STACK_USAGE_HEADER_BEFORE_MERGE)
                set(R_STACK_USAGE_CONVERGED TRUE)
            endif()
        endif()
    endwhile()

    if(R_STACK_USAGE_INSTRUMENTED)
        if(NOT EXISTS "${R_FINAL_STACK_USAGE_REPORT}" OR
           IS_DIRECTORY "${R_FINAL_STACK_USAGE_REPORT}")
            message(FATAL_ERROR
                "sanitizer diagnostic compile did not produce a stack-usage report: "
                "${R_FINAL_STACK_USAGE_REPORT}")
        endif()
        file(SIZE "${R_FINAL_STACK_USAGE_REPORT}" R_FINAL_STACK_USAGE_REPORT_SIZE)
        if(R_FINAL_STACK_USAGE_REPORT_SIZE EQUAL 0)
            message(FATAL_ERROR
                "sanitizer diagnostic compile produced an empty stack-usage report: "
                "${R_FINAL_STACK_USAGE_REPORT}")
        endif()
        file(WRITE "${R_STACK_USAGE_NONCONFORMING_MARKER}"
            "diagnostic-only sanitizer build; stack conformance is not claimed\n")
        message(STATUS
            "sanitizer stack-usage is diagnostic-only and nonconforming; "
            "dynamic instrumented frames are not validated; marker: "
            "${R_STACK_USAGE_NONCONFORMING_MARKER}")
    else()
        set(STACK_USAGE_OBJECT "${R_FINAL_STACK_USAGE_OBJECT}")
        set(STACK_USAGE_REPORT "${R_FINAL_STACK_USAGE_REPORT}")
        set(STACK_USAGE_MODE "validate")
        include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_stack_usage.cmake")
        message(STATUS
            "stack bounds converged after ${R_STACK_USAGE_ITERATION} candidate compile(s); "
            "linking exact object ${R_FINAL_STACK_USAGE_OBJECT}")
    endif()

    if(GENERATED_C_OPTIMIZATION STREQUAL "-O0")
        set(R_CODEGEN_LINK_INPUT "${R_FINAL_STACK_USAGE_OBJECT}")
        set(R_CODEGEN_LINK_GENERATED_FLAGS)
    else()
        set(R_CODEGEN_LINK_INPUT "${R_CODEGEN_COMPILE_SOURCE}")
        set(R_CODEGEN_LINK_GENERATED_FLAGS
            ${R_CODEGEN_WRAPPER_FLAGS} -include "${R_STACK_USAGE_HEADER}")
        message(STATUS
            "recompiling generated C17 with ${GENERATED_C_OPTIMIZATION}; "
            "runtime stack requirements use the conservative O0 bounds")
    endif()
endif()

execute_process(
    COMMAND "${C_COMPILER}"
        ${R_CODEGEN_PLATFORM_FLAGS}
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
        ${GENERATED_C_OPTIMIZATION}
        ${GENERATED_C_EXTRA_FLAGS}
        ${R_CODEGEN_SANITIZER_FLAGS}
        ${R_CODEGEN_LINK_GENERATED_FLAGS}
        ${R_CODEGEN_HEADER_INCLUDE_FLAGS}
        "-I${RUNTIME_INCLUDE}"
        "-I${RUNTIME_DARWIN_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/core/include"
        "-I${R_STD_ALLOC_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/arc/include"
        "-I${R_LIBRARY_ROOT}/std/array/include"
        "-I${R_STD_ASYNC_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/bits/include"
        "-I${R_LIBRARY_ROOT}/std/bytes/include"
        "-I${R_LIBRARY_ROOT}/std/hash/include"
        "-I${R_LIBRARY_ROOT}/std/secret/include"
        "-I${R_LIBRARY_ROOT}/std/random/include"
        "-I${R_LIBRARY_ROOT}/std/signal/include"
        "-I${R_LIBRARY_ROOT}/std/test/include"
        "-I${R_STD_C_INCLUDE}"
        "-I${R_STD_CONVERT_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/dict/include"
        "-I${R_LIBRARY_ROOT}/std/env/include"
        "-I${R_STD_ERROR_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/format/include"
            "-I${R_LIBRARY_ROOT}/std/json/include"
        "-I${R_LIBRARY_ROOT}/std/fs/include"
        "-I${R_LIBRARY_ROOT}/std/io/include"
        "-I${R_LIBRARY_ROOT}/std/list/include"
        "-I${R_LIBRARY_ROOT}/std/math/include"
        "-I${R_LIBRARY_ROOT}/std/net/include"
        "-I${R_LIBRARY_ROOT}/std/process/include"
        "-I${R_LIBRARY_ROOT}/std/rc/include"
        "-I${R_STD_STRING_INCLUDE}"
        "-I${R_LIBRARY_ROOT}/std/sync/include"
        "-I${R_LIBRARY_ROOT}/std/thread/include"
        "-I${R_LIBRARY_ROOT}/std/time/include"
        "-I${R_LIBRARY_ROOT}/std/utf8/include"
        "${R_CODEGEN_LINK_INPUT}"
        ${R_CODEGEN_EXTRA_C_SOURCES}
        ${R_CODEGEN_STD_NATIVE_ARCHIVES}
        "${R_STD_ALLOC_LIBRARY}"
        "${R_STD_ARC_LIBRARY}"
        "${R_STD_ARRAY_LIBRARY}"
        "${R_STD_LIST_LIBRARY}"
        "${R_STD_DICT_LIBRARY}"
        "${R_STD_BYTES_LIBRARY}"
        "${R_STD_HASH_LIBRARY}"
        "${R_STD_SECRET_LIBRARY}"
        "${R_STD_RANDOM_LIBRARY}"
        "${R_STD_MATH_LIBRARY}"
        "${R_STD_FORMAT_LIBRARY}"
        "${R_STD_JSON_LIBRARY}"
        "${R_STD_NET_LIBRARY}"
        "${R_STD_SIGNAL_LIBRARY}"
        "${R_STD_TEST_LIBRARY}"
        "${R_STD_PROCESS_LIBRARY}"
        "${R_STD_STRING_LIBRARY}"
        "${R_STD_UTF8_LIBRARY}"
        "${R_STD_BITS_LIBRARY}"
        "${R_CORE_LIBRARY}"
        "${R_STD_ASYNC_LIBRARY}"
        "${R_STD_C_LIBRARY}"
        "${R_STD_CONVERT_LIBRARY}"
        "${R_STD_ENV_LIBRARY}"
        "${R_STD_ERROR_LIBRARY}"
        "${R_STD_FS_LIBRARY}"
        "${R_STD_IO_LIBRARY}"
        "${R_STD_TIME_LIBRARY}"
        "${R_STD_RC_LIBRARY}"
        "${R_STD_SYNC_LIBRARY}"
        "${R_STD_THREAD_LIBRARY}"
        "${RUNTIME_ALLOCATOR_SOURCE}"
        "${RUNTIME_ARRAY_SOURCE}"
        "${RUNTIME_LIST_SOURCE}"
        "${RUNTIME_DICT_SOURCE}"
        "${RUNTIME_ARC_SOURCE}"
        "${RUNTIME_RC_SOURCE}"
        "${RUNTIME_DROP_SOURCE}"
        "${RUNTIME_OWN_SOURCE}"
        "${RUNTIME_HOSTED_SOURCE}"
        "${RUNTIME_THREAD_ATTACHMENT_SOURCE}"
        "${RUNTIME_TASK_SOURCE}"
        "${RUNTIME_STACK_SOURCE}"
        "${RUNTIME_DARWIN_SOCKET_LIBRARY}"
        "${RUNTIME_DARWIN_DNS_LIBRARY}"
        "${RUNTIME_DARWIN_IO_LIBRARY}"
        "${RUNTIME_DARWIN_PROCESS_LIBRARY}"
        "${RUNTIME_FS_LANE_SOURCE}"
        "${RUNTIME_FS_LANE_ASYNC_SOURCE}"
        "${RUNTIME_FS_LANE_SYSCALLS_SOURCE}"
        "${RUNTIME_FS_SERVICE_SOURCE}"
        "${RUNTIME_STRING_SOURCE}"
        "${RUNTIME_UTF8_SOURCE}"
        "${RUNTIME_PANIC_SOURCE}"
        "${RUNTIME_DARWIN_EVENT_LIBRARY}"
        "${RUNTIME_DARWIN_TIMER_LIBRARY}"
        -o "${OUTPUT_EXE}"
    RESULT_VARIABLE R_COMPILE_RESULT
    OUTPUT_VARIABLE R_COMPILE_OUTPUT
    ERROR_VARIABLE R_COMPILE_ERROR
)
if(NOT R_COMPILE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "generated C17 did not compile (${R_COMPILE_RESULT}):\n"
        "${R_COMPILE_OUTPUT}${R_COMPILE_ERROR}")
endif()

if(DEFINED COMPILE_ONLY AND COMPILE_ONLY)
    if(NOT EXISTS "${OUTPUT_EXE}" OR IS_DIRECTORY "${OUTPUT_EXE}")
        message(FATAL_ERROR
            "generated C17 link reported success but produced no executable: "
            "${OUTPUT_EXE}")
    endif()
    file(SIZE "${OUTPUT_EXE}" R_CODEGEN_EXECUTABLE_SIZE)
    if(R_CODEGEN_EXECUTABLE_SIZE EQUAL 0)
        message(FATAL_ERROR
            "generated C17 link produced an empty executable: ${OUTPUT_EXE}")
    endif()
    return()
endif()

set(R_CODEGEN_CREATED_DIRECTORY_ROOT "")
set(R_CODEGEN_CREATED_DIRECTORY_PATH "")
set(R_CODEGEN_ATOMIC_WRITE_ROOT "")
set(R_CODEGEN_ATOMIC_WRITE_PATH "")
if(DEFINED CREATE_DIRECTORY_RELATIVE AND NOT CREATE_DIRECTORY_RELATIVE STREQUAL "" AND
   DEFINED ATOMIC_WRITE_RELATIVE AND NOT ATOMIC_WRITE_RELATIVE STREQUAL "")
    message(FATAL_ERROR
        "CREATE_DIRECTORY_RELATIVE and ATOMIC_WRITE_RELATIVE are mutually exclusive")
endif()
if(DEFINED CREATE_DIRECTORY_RELATIVE AND NOT CREATE_DIRECTORY_RELATIVE STREQUAL "")
    if(IS_ABSOLUTE "${CREATE_DIRECTORY_RELATIVE}" OR
       CREATE_DIRECTORY_RELATIVE MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR
            "CREATE_DIRECTORY_RELATIVE must be a non-escaping relative path")
    endif()
    set(R_CODEGEN_CREATED_DIRECTORY_ROOT "${OUTPUT_EXE}.root")
    set(R_CODEGEN_CREATED_DIRECTORY_PATH
        "${R_CODEGEN_CREATED_DIRECTORY_ROOT}/${CREATE_DIRECTORY_RELATIVE}")
    file(REMOVE_RECURSE "${R_CODEGEN_CREATED_DIRECTORY_ROOT}")
    file(MAKE_DIRECTORY "${R_CODEGEN_CREATED_DIRECTORY_ROOT}")
endif()
if(DEFINED ATOMIC_WRITE_RELATIVE AND NOT ATOMIC_WRITE_RELATIVE STREQUAL "")
    if(NOT DEFINED ATOMIC_WRITE_SOURCE OR ATOMIC_WRITE_SOURCE STREQUAL "" OR
       NOT EXISTS "${ATOMIC_WRITE_SOURCE}" OR IS_DIRECTORY "${ATOMIC_WRITE_SOURCE}")
        message(FATAL_ERROR "ATOMIC_WRITE_SOURCE must name an existing regular file")
    endif()
    if(IS_ABSOLUTE "${ATOMIC_WRITE_RELATIVE}" OR
       ATOMIC_WRITE_RELATIVE MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "ATOMIC_WRITE_RELATIVE must be a non-escaping relative path")
    endif()
    if(NOT DEFINED ATOMIC_WRITE_REPLACEMENT_SOURCE OR
       ATOMIC_WRITE_REPLACEMENT_SOURCE STREQUAL "" OR
       NOT EXISTS "${ATOMIC_WRITE_REPLACEMENT_SOURCE}" OR
       IS_DIRECTORY "${ATOMIC_WRITE_REPLACEMENT_SOURCE}")
        message(FATAL_ERROR
            "ATOMIC_WRITE_REPLACEMENT_SOURCE must name an existing regular file")
    endif()
    file(SHA256 "${ATOMIC_WRITE_SOURCE}" R_CODEGEN_ATOMIC_WRITE_SOURCE_HASH)
    file(SHA256
        "${ATOMIC_WRITE_REPLACEMENT_SOURCE}"
        R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_HASH)
    if(R_CODEGEN_ATOMIC_WRITE_SOURCE_HASH STREQUAL
       R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_HASH)
        message(FATAL_ERROR "atomic-write test sources must have different contents")
    endif()
    set(R_CODEGEN_ATOMIC_WRITE_ROOT "${OUTPUT_EXE}.root")
    set(R_CODEGEN_ATOMIC_WRITE_PATH
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}/${ATOMIC_WRITE_RELATIVE}")
    file(REMOVE_RECURSE "${R_CODEGEN_ATOMIC_WRITE_ROOT}")
    file(MAKE_DIRECTORY "${R_CODEGEN_ATOMIC_WRITE_ROOT}")
endif()

set(R_CODEGEN_RUN_COMMAND "${OUTPUT_EXE}")
if(DEFINED ENABLE_SANITIZERS AND ENABLE_SANITIZERS)
    set(ENV{ASAN_OPTIONS} "abort_on_error=1")
    set(ENV{UBSAN_OPTIONS} "halt_on_error=1:print_stacktrace=1")
elseif(DEFINED ENABLE_THREAD_SANITIZER AND ENABLE_THREAD_SANITIZER)
    set(ENV{TSAN_OPTIONS} "halt_on_error=1")
endif()
set(R_CODEGEN_RUN_COMMAND_BASE ${R_CODEGEN_RUN_COMMAND})
if(DEFINED PROGRAM_ARGUMENTS AND NOT PROGRAM_ARGUMENTS STREQUAL "")
    list(APPEND R_CODEGEN_RUN_COMMAND ${PROGRAM_ARGUMENTS})
endif()
if(NOT R_CODEGEN_CREATED_DIRECTORY_ROOT STREQUAL "")
    list(APPEND R_CODEGEN_RUN_COMMAND
        "${R_CODEGEN_CREATED_DIRECTORY_ROOT}"
        "${CREATE_DIRECTORY_RELATIVE}")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "")
    list(APPEND R_CODEGEN_RUN_COMMAND
        "${ATOMIC_WRITE_SOURCE}"
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}"
        "${ATOMIC_WRITE_RELATIVE}")
endif()

if(DEFINED PROGRAM_INPUT_FILE AND NOT PROGRAM_INPUT_FILE STREQUAL "")
    if(NOT EXISTS "${PROGRAM_INPUT_FILE}" OR IS_DIRECTORY "${PROGRAM_INPUT_FILE}")
        message(FATAL_ERROR "PROGRAM_INPUT_FILE must name an existing regular file")
    endif()
    execute_process(
        COMMAND ${R_CODEGEN_RUN_COMMAND}
        INPUT_FILE "${PROGRAM_INPUT_FILE}"
        RESULT_VARIABLE R_RUN_RESULT
        OUTPUT_VARIABLE R_RUN_OUTPUT
        ERROR_VARIABLE R_RUN_ERROR
    )
else()
    execute_process(
        COMMAND ${R_CODEGEN_RUN_COMMAND}
        RESULT_VARIABLE R_RUN_RESULT
        OUTPUT_VARIABLE R_RUN_OUTPUT
        ERROR_VARIABLE R_RUN_ERROR
    )
endif()
set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT 0)
set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_OUTPUT "")
set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_ERROR "")
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND "${R_RUN_RESULT}" STREQUAL "0")
    set(R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_COMMAND ${R_CODEGEN_RUN_COMMAND_BASE})
    list(APPEND R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_COMMAND
        "${ATOMIC_WRITE_REPLACEMENT_SOURCE}"
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}"
        "${ATOMIC_WRITE_RELATIVE}")
    execute_process(
        COMMAND ${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_COMMAND}
        RESULT_VARIABLE R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT
        OUTPUT_VARIABLE R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_OUTPUT
        ERROR_VARIABLE R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_ERROR
    )
endif()
set(R_CODEGEN_CREATED_DIRECTORY_FOUND FALSE)
set(R_CODEGEN_ATOMIC_WRITE_MATCHED FALSE)
set(R_CODEGEN_ATOMIC_WRITE_STAGING_CLEAN FALSE)
if(NOT R_CODEGEN_CREATED_DIRECTORY_ROOT STREQUAL "")
    if(IS_DIRECTORY "${R_CODEGEN_CREATED_DIRECTORY_PATH}")
        set(R_CODEGEN_CREATED_DIRECTORY_FOUND TRUE)
    endif()
    file(REMOVE_RECURSE "${R_CODEGEN_CREATED_DIRECTORY_ROOT}")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "")
    if(EXISTS "${R_CODEGEN_ATOMIC_WRITE_PATH}" AND
       NOT IS_DIRECTORY "${R_CODEGEN_ATOMIC_WRITE_PATH}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${ATOMIC_WRITE_SOURCE}"
                "${R_CODEGEN_ATOMIC_WRITE_PATH}"
            RESULT_VARIABLE R_CODEGEN_ATOMIC_WRITE_COMPARE_RESULT
        )
        if(R_CODEGEN_ATOMIC_WRITE_COMPARE_RESULT EQUAL 0)
            set(R_CODEGEN_ATOMIC_WRITE_MATCHED TRUE)
        endif()
    endif()
    file(GLOB R_CODEGEN_ATOMIC_WRITE_STAGING
        "${R_CODEGEN_ATOMIC_WRITE_ROOT}/.r-dir-stage-*")
    if(NOT R_CODEGEN_ATOMIC_WRITE_STAGING)
        set(R_CODEGEN_ATOMIC_WRITE_STAGING_CLEAN TRUE)
    endif()
    file(REMOVE_RECURSE "${R_CODEGEN_ATOMIC_WRITE_ROOT}")
endif()
if(DEFINED EXPECTED_PANIC AND NOT EXPECTED_PANIC STREQUAL "")
    if("${R_RUN_RESULT}" STREQUAL "0")
        message(FATAL_ERROR
            "generated panic program completed successfully")
    endif()
    string(FIND "${R_RUN_ERROR}" "${EXPECTED_PANIC}" R_PANIC_OFFSET)
    if(R_PANIC_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "panic stderr does not contain '${EXPECTED_PANIC}': "
            "${R_RUN_ERROR}")
    endif()
    if(DEFINED SOURCE_1 AND SOURCE_1 MATCHES "codegen_abort_skips_finally\\.r$")
        if(NOT R_RUN_RESULT MATCHES "[Aa]borted")
            message(FATAL_ERROR
                "abort-strategy finally fixture did not terminate through SIGABRT: "
                "${R_RUN_RESULT}")
        endif()
        string(FIND
            "${R_RUN_ERROR}"
            "division_by_zero"
            R_UNSTARTED_FINALLY_PANIC_OFFSET)
        if(NOT R_UNSTARTED_FINALLY_PANIC_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "abort strategy executed a not-yet-started finally: ${R_RUN_ERROR}")
        endif()
    endif()
elseif(NOT "${R_RUN_RESULT}" STREQUAL "0")
    if(NOT DEFINED EXPECTED_EXIT_STATUS OR EXPECTED_EXIT_STATUS STREQUAL "" OR
       NOT "${R_RUN_RESULT}" STREQUAL "${EXPECTED_EXIT_STATUS}")
        message(FATAL_ERROR
            "generated program failed (${R_RUN_RESULT}): "
            "${R_RUN_OUTPUT}${R_RUN_ERROR}")
    endif()
elseif(DEFINED EXPECTED_EXIT_STATUS AND NOT EXPECTED_EXIT_STATUS STREQUAL "0")
    message(FATAL_ERROR
        "generated program returned 0 instead of ${EXPECTED_EXIT_STATUS}: "
        "${R_RUN_OUTPUT}${R_RUN_ERROR}")
endif()
if(DEFINED CHECK_STDOUT AND CHECK_STDOUT AND
   NOT "${R_RUN_OUTPUT}" STREQUAL "${EXPECTED_STDOUT}")
    message(FATAL_ERROR
        "generated program stdout mismatch: expected '${EXPECTED_STDOUT}', "
        "observed '${R_RUN_OUTPUT}'")
endif()
if(NOT R_CODEGEN_CREATED_DIRECTORY_ROOT STREQUAL "" AND
   NOT R_CODEGEN_CREATED_DIRECTORY_FOUND)
    message(FATAL_ERROR
        "generated program did not create '${CREATE_DIRECTORY_RELATIVE}' beneath its private root")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND NOT R_CODEGEN_ATOMIC_WRITE_MATCHED)
    message(FATAL_ERROR
        "generated program did not atomically publish byte-identical '${ATOMIC_WRITE_RELATIVE}'")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND
   NOT "${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR
        "generated no-replace retry failed (${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_RESULT}): "
        "${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_OUTPUT}"
        "${R_CODEGEN_ATOMIC_WRITE_REPLACEMENT_ERROR}")
endif()
if(NOT R_CODEGEN_ATOMIC_WRITE_ROOT STREQUAL "" AND
   NOT R_CODEGEN_ATOMIC_WRITE_STAGING_CLEAN)
    message(FATAL_ERROR
        "generated program left private atomic-write staging entries beneath its root")
endif()

if(DEFINED EXPECTED_STDERR AND NOT EXPECTED_STDERR STREQUAL "")
    string(FIND "${R_RUN_ERROR}" "${EXPECTED_STDERR}" R_EXPECTED_DIAGNOSTIC_OFFSET)
    if(R_EXPECTED_DIAGNOSTIC_OFFSET EQUAL -1)
        message(FATAL_ERROR "missing main diagnostic '${EXPECTED_STDERR}': ${R_RUN_ERROR}")
    endif()
endif()
