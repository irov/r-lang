# Builds and runs one freestanding-profile program (Core R-CONF-0005, Annex G.3).
#
# The generated C17 is compiled with -ffreestanding against the freestanding runtime headers
# only; its undefined symbols shall stay inside ALLOWED_UNDEFINED_SYMBOLS. The freestanding
# runtime and the core library are compiled the same way. A hosted environment source then
# supplies the panic handler and the stack bounds, links everything and runs the program.
foreach(required IN ITEMS
        R_FRONT_EXECUTABLE C_COMPILER PYTHON_EXECUTABLE TARGET_MANIFEST SOURCE
        ENVIRONMENT_SOURCE ALLOWED_UNDEFINED_SYMBOLS PROJECT_ROOT OUTPUT_DIR
        EXPECTED_STDOUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "freestanding program test requires ${required}")
    endif()
endforeach()

set(R_FS_PLATFORM_FLAGS)
if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        RESULT_VARIABLE R_FS_SDK_RESULT
        OUTPUT_VARIABLE R_FS_SDK_PATH
        ERROR_VARIABLE R_FS_SDK_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT R_FS_SDK_RESULT EQUAL 0 OR R_FS_SDK_PATH STREQUAL "")
        message(FATAL_ERROR "could not resolve the macOS SDK path: ${R_FS_SDK_ERROR}")
    endif()
    list(APPEND R_FS_PLATFORM_FLAGS -isysroot "${R_FS_SDK_PATH}")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(R_FS_C "${OUTPUT_DIR}/program.c")
set(R_FS_EXE "${OUTPUT_DIR}/program")
set(R_FS_WARNINGS
    -std=c17 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow
    -Wstrict-prototypes -Wmissing-prototypes)
# Include order: the freestanding header directory first so that r_runtime_target_abi.h resolves
# to the freestanding variant generated from TARGET_MANIFEST.
set(R_FS_INCLUDES
    "-I${PROJECT_ROOT}/runtime/freestanding/include"
    "-I${PROJECT_ROOT}/runtime/include"
    "-I${PROJECT_ROOT}/library/core/include")
set(R_FS_FREESTANDING_FLAGS -ffreestanding -fno-stack-protector -fno-builtin)

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --profile freestanding
        --target-manifest "${TARGET_MANIFEST}"
        --emit=c17
        "${SOURCE}"
    RESULT_VARIABLE R_FS_EMIT_RESULT
    OUTPUT_FILE "${R_FS_C}"
    ERROR_VARIABLE R_FS_EMIT_ERROR
)
if(NOT R_FS_EMIT_RESULT EQUAL 0)
    message(FATAL_ERROR "r-front --profile freestanding failed (${R_FS_EMIT_RESULT}): ${R_FS_EMIT_ERROR}")
endif()
file(READ "${R_FS_C}" R_FS_GENERATED)
if(R_FS_GENERATED MATCHES "r_runtime_0_1\\.h" OR R_FS_GENERATED MATCHES "int main\\(" OR
   R_FS_GENERATED MATCHES "RRuntimeCEntryGuard" OR R_FS_GENERATED MATCHES "r_runtime_hosted_")
    message(FATAL_ERROR "freestanding generated C references the hosted runtime")
endif()

# Stack bounds: bootstrap measurement, fixed point, final object (R-FUNC-0004).
set(R_FS_STACK_OBJECT "${R_FS_C}.stack-usage.o")
set(R_FS_STACK_REPORT "${R_FS_C}.stack-usage.su")
set(R_FS_STACK_HEADER "${R_FS_C}.stack-usage.h")
set(R_FS_FINAL_OBJECT "${R_FS_C}.final.o")
set(R_FS_FINAL_REPORT "${R_FS_C}.final.su")
file(REMOVE "${R_FS_STACK_OBJECT}" "${R_FS_STACK_REPORT}" "${R_FS_STACK_HEADER}"
    "${R_FS_FINAL_OBJECT}" "${R_FS_FINAL_REPORT}")
set(R_FS_MEASURE_FLAGS -O0 -c -fstack-usage -fno-inline-functions -fno-lto)
execute_process(
    COMMAND "${C_COMPILER}" ${R_FS_PLATFORM_FLAGS} ${R_FS_WARNINGS} ${R_FS_FREESTANDING_FLAGS}
        ${R_FS_MEASURE_FLAGS} -DR_STACK_USAGE_MEASUREMENT=1 ${R_FS_INCLUDES}
        "${R_FS_C}" -o "${R_FS_STACK_OBJECT}"
    RESULT_VARIABLE R_FS_MEASURE_RESULT
    OUTPUT_VARIABLE R_FS_MEASURE_OUTPUT
    ERROR_VARIABLE R_FS_MEASURE_ERROR
)
if(NOT R_FS_MEASURE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "freestanding stack-usage compile failed (${R_FS_MEASURE_RESULT}):\n"
        "${R_FS_MEASURE_OUTPUT}${R_FS_MEASURE_ERROR}")
endif()
file(READ "${TARGET_MANIFEST}" R_FS_MANIFEST_JSON)
string(JSON R_FS_ENTRY_BUDGET ERROR_VARIABLE R_FS_BUDGET_ERROR
    GET "${R_FS_MANIFEST_JSON}" core stack entry_budget_bytes)
if(NOT R_FS_BUDGET_ERROR STREQUAL "NOTFOUND" OR NOT R_FS_ENTRY_BUDGET MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "freestanding manifest has no valid entry stack budget")
endif()
string(JSON R_FS_MAX_ITERATIONS ERROR_VARIABLE R_FS_ITERATION_ERROR
    GET "${R_FS_MANIFEST_JSON}" core stack frame_measurement artifact_pipeline fixed_point
    maximum_candidate_iterations)
if(NOT R_FS_ITERATION_ERROR STREQUAL "NOTFOUND" OR NOT R_FS_MAX_ITERATIONS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "freestanding manifest has no valid stack fixed-point iteration limit")
endif()
set(STACK_USAGE_OBJECT "${R_FS_STACK_OBJECT}")
set(STACK_USAGE_REPORT "${R_FS_STACK_REPORT}")
set(STACK_USAGE_SOURCE "${R_FS_C}")
set(STACK_USAGE_BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(STACK_USAGE_HEADER "${R_FS_STACK_HEADER}")
set(STACK_USAGE_MODE "generate")
set(STACK_USAGE_PYTHON "${PYTHON_EXECUTABLE}")
set(STACK_USAGE_ENTRY_TOOL "${PROJECT_ROOT}/tools/compute_stack_entries.py")
set(STACK_USAGE_ENTRY_BUDGET "${R_FS_ENTRY_BUDGET}")
include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_stack_usage.cmake")
set(R_FS_CONVERGED FALSE)
set(R_FS_ITERATION 0)
while(NOT R_FS_CONVERGED)
    math(EXPR R_FS_ITERATION "${R_FS_ITERATION} + 1")
    if(R_FS_ITERATION GREATER R_FS_MAX_ITERATIONS)
        message(FATAL_ERROR "freestanding stack bounds did not reach a fixed point")
    endif()
    execute_process(
        COMMAND "${C_COMPILER}" ${R_FS_PLATFORM_FLAGS} ${R_FS_WARNINGS} ${R_FS_FREESTANDING_FLAGS}
            ${R_FS_MEASURE_FLAGS} -include "${R_FS_STACK_HEADER}" ${R_FS_INCLUDES}
            "${R_FS_C}" -o "${R_FS_FINAL_OBJECT}"
        RESULT_VARIABLE R_FS_FINAL_RESULT
        OUTPUT_VARIABLE R_FS_FINAL_OUTPUT
        ERROR_VARIABLE R_FS_FINAL_ERROR
    )
    if(NOT R_FS_FINAL_RESULT EQUAL 0)
        message(FATAL_ERROR
            "freestanding stack candidate ${R_FS_ITERATION} failed (${R_FS_FINAL_RESULT}):\n"
            "${R_FS_FINAL_OUTPUT}${R_FS_FINAL_ERROR}")
    endif()
    file(READ "${R_FS_STACK_HEADER}" R_FS_HEADER_BEFORE)
    set(STACK_USAGE_OBJECT "${R_FS_FINAL_OBJECT}")
    set(STACK_USAGE_REPORT "${R_FS_FINAL_REPORT}")
    set(STACK_USAGE_MODE "merge")
    include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_stack_usage.cmake")
    file(READ "${R_FS_STACK_HEADER}" R_FS_HEADER_AFTER)
    if(R_FS_HEADER_AFTER STREQUAL R_FS_HEADER_BEFORE)
        set(R_FS_CONVERGED TRUE)
    endif()
endwhile()
set(STACK_USAGE_OBJECT "${R_FS_FINAL_OBJECT}")
set(STACK_USAGE_REPORT "${R_FS_FINAL_REPORT}")
set(STACK_USAGE_MODE "validate")
include("${CMAKE_CURRENT_LIST_DIR}/check_codegen_stack_usage.cmake")

# The generated object may need only the core runtime, the environment handler and the
# freestanding C library symbols the compiler synthesizes for aggregate copies.
execute_process(
    COMMAND nm -u "${R_FS_FINAL_OBJECT}"
    RESULT_VARIABLE R_FS_NM_RESULT
    OUTPUT_VARIABLE R_FS_NM_OUTPUT
    ERROR_VARIABLE R_FS_NM_ERROR
)
if(NOT R_FS_NM_RESULT EQUAL 0)
    message(FATAL_ERROR "nm -u failed (${R_FS_NM_RESULT}): ${R_FS_NM_ERROR}")
endif()
file(STRINGS "${ALLOWED_UNDEFINED_SYMBOLS}" R_FS_ALLOWED)
string(REGEX REPLACE "[ \t\r]+" "" R_FS_NM_OUTPUT "${R_FS_NM_OUTPUT}")
string(REPLACE "\n" ";" R_FS_UNDEFINED "${R_FS_NM_OUTPUT}")
set(R_FS_UNEXPECTED)
foreach(symbol IN LISTS R_FS_UNDEFINED)
    if(symbol STREQUAL "" OR symbol MATCHES ":$")
        continue()
    endif()
    list(FIND R_FS_ALLOWED "${symbol}" R_FS_ALLOWED_INDEX)
    if(R_FS_ALLOWED_INDEX EQUAL -1)
        list(APPEND R_FS_UNEXPECTED "${symbol}")
    endif()
endforeach()
if(R_FS_UNEXPECTED)
    message(FATAL_ERROR
        "freestanding program depends on symbols outside the environment contract: "
        "${R_FS_UNEXPECTED}")
endif()

# The freestanding runtime and the core library compile as freestanding C17 too.
set(R_FS_RUNTIME_OBJECTS)
foreach(source IN ITEMS
        runtime/freestanding/source/panic.c
        runtime/freestanding/source/stack.c
        runtime/freestanding/source/thread_local.c
        runtime/source/utf8.c
        library/core/source/validate_utf8.c)
    get_filename_component(R_FS_SOURCE_NAME "${source}" NAME_WE)
    set(R_FS_OBJECT "${OUTPUT_DIR}/${R_FS_SOURCE_NAME}.freestanding.o")
    execute_process(
        COMMAND "${C_COMPILER}" ${R_FS_PLATFORM_FLAGS} ${R_FS_WARNINGS} ${R_FS_FREESTANDING_FLAGS}
            -O0 -c ${R_FS_INCLUDES} "${PROJECT_ROOT}/${source}" -o "${R_FS_OBJECT}"
        RESULT_VARIABLE R_FS_RUNTIME_RESULT
        OUTPUT_VARIABLE R_FS_RUNTIME_OUTPUT
        ERROR_VARIABLE R_FS_RUNTIME_ERROR
    )
    if(NOT R_FS_RUNTIME_RESULT EQUAL 0)
        message(FATAL_ERROR
            "freestanding runtime compile of ${source} failed (${R_FS_RUNTIME_RESULT}):\n"
            "${R_FS_RUNTIME_OUTPUT}${R_FS_RUNTIME_ERROR}")
    endif()
    list(APPEND R_FS_RUNTIME_OBJECTS "${R_FS_OBJECT}")
endforeach()

# The hosted environment supplies the panic handler and stack bounds and links the program.
execute_process(
    COMMAND "${C_COMPILER}" ${R_FS_PLATFORM_FLAGS} ${R_FS_WARNINGS} -O0 ${R_FS_INCLUDES}
        "${ENVIRONMENT_SOURCE}" "${R_FS_FINAL_OBJECT}" ${R_FS_RUNTIME_OBJECTS}
        -o "${R_FS_EXE}"
    RESULT_VARIABLE R_FS_LINK_RESULT
    OUTPUT_VARIABLE R_FS_LINK_OUTPUT
    ERROR_VARIABLE R_FS_LINK_ERROR
)
if(NOT R_FS_LINK_RESULT EQUAL 0)
    message(FATAL_ERROR
        "freestanding program link failed (${R_FS_LINK_RESULT}):\n"
        "${R_FS_LINK_OUTPUT}${R_FS_LINK_ERROR}")
endif()
execute_process(
    COMMAND "${R_FS_EXE}"
    RESULT_VARIABLE R_FS_RUN_RESULT
    OUTPUT_VARIABLE R_FS_RUN_OUTPUT
    ERROR_VARIABLE R_FS_RUN_ERROR
)
if(NOT R_FS_RUN_RESULT EQUAL 0)
    message(FATAL_ERROR
        "freestanding program failed (${R_FS_RUN_RESULT}):\n${R_FS_RUN_OUTPUT}${R_FS_RUN_ERROR}")
endif()
string(FIND "${R_FS_RUN_OUTPUT}" "${EXPECTED_STDOUT}" R_FS_STDOUT_OFFSET)
if(R_FS_STDOUT_OFFSET EQUAL -1)
    message(FATAL_ERROR "freestanding program output lacks '${EXPECTED_STDOUT}': ${R_FS_RUN_OUTPUT}")
endif()
