execute_process(
    COMMAND "${RE2C_EXECUTABLE}" --version
    OUTPUT_VARIABLE RE2C_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE RE2C_VERSION_RESULT
)

if(NOT RE2C_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR "Unable to execute re2c")
endif()

if(NOT RE2C_VERSION_OUTPUT MATCHES "re2c 4\\.5\\.1")
    message(FATAL_ERROR "Lexer regeneration requires re2c 4.5.1, got: ${RE2C_VERSION_OUTPUT}")
endif()

set(GENERATED_FILE "${OUTPUT_FILE}")
if(VERIFY_ONLY)
    set(GENERATED_FILE "${OUTPUT_FILE}.verify")
endif()

execute_process(
    COMMAND "${RE2C_EXECUTABLE}" --no-debug-info --no-generation-date -W -Werror
            -o "${GENERATED_FILE}" "${SOURCE_FILE}"
    RESULT_VARIABLE RE2C_RESULT
)

if(NOT RE2C_RESULT EQUAL 0)
    message(FATAL_ERROR "re2c lexer generation failed")
endif()

if(VERIFY_ONLY)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUTPUT_FILE}" "${GENERATED_FILE}"
        RESULT_VARIABLE COMPARE_RESULT
    )
    file(REMOVE "${GENERATED_FILE}")
    if(NOT COMPARE_RESULT EQUAL 0)
        message(FATAL_ERROR "committed lexer_generated.c is stale")
    endif()
endif()
