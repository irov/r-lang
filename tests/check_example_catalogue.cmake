execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD_DIRECTORY}" --show-only=json-v1
    RESULT_VARIABLE status OUTPUT_FILE "${BUILD_DIRECTORY}/example-registered-tests.json"
    ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Cannot list example tests: ${error}")
endif()
execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${SOURCE_DIRECTORY}/tools/check_example_coverage.py"
        --frontend "${R_FRONT_EXECUTABLE}" --check --require-complete --ctest-json "${BUILD_DIRECTORY}/example-registered-tests.json"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Example catalogue is inconsistent: ${output}${error}")
endif()
message(STATUS "${output}")
