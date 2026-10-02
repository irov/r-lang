# Builds the benchmark pairs and verifies that each R program and its C mirror agree.
if(NOT DEFINED BUILD_DIR OR NOT DEFINED PYTHON OR NOT DEFINED HARNESS OR NOT DEFINED PAIRS)
    message(FATAL_ERROR "missing benchmark pair check input")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target bench_programs
    RESULT_VARIABLE R_BENCH_BUILD_RESULT
    OUTPUT_VARIABLE R_BENCH_BUILD_OUTPUT
    ERROR_VARIABLE R_BENCH_BUILD_ERROR
)
if(NOT R_BENCH_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR
        "benchmark programs did not build (${R_BENCH_BUILD_RESULT}):\n"
        "${R_BENCH_BUILD_OUTPUT}${R_BENCH_BUILD_ERROR}")
endif()
string(REPLACE "|" ";" R_BENCH_PAIR_LIST "${PAIRS}")
set(R_BENCH_PAIR_ARGUMENTS)
foreach(R_BENCH_PAIR IN LISTS R_BENCH_PAIR_LIST)
    list(APPEND R_BENCH_PAIR_ARGUMENTS --pair "${R_BENCH_PAIR}")
endforeach()
execute_process(
    COMMAND "${PYTHON}" "${HARNESS}" --check ${R_BENCH_PAIR_ARGUMENTS}
    RESULT_VARIABLE R_BENCH_CHECK_RESULT
    OUTPUT_VARIABLE R_BENCH_CHECK_OUTPUT
    ERROR_VARIABLE R_BENCH_CHECK_ERROR
)
if(NOT R_BENCH_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR
        "benchmark pairs disagree (${R_BENCH_CHECK_RESULT}):\n"
        "${R_BENCH_CHECK_OUTPUT}${R_BENCH_CHECK_ERROR}")
endif()
message(STATUS "${R_BENCH_CHECK_OUTPUT}")
