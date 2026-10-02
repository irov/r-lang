if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED MODULE_MAP OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "r-front zip test is missing an input")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --module-map "${MODULE_MAP}" --emit=ast
    OUTPUT_FILE "${OUTPUT_FILE}"
    ERROR_VARIABLE FRONTEND_DIAGNOSTICS
    RESULT_VARIABLE FRONTEND_RESULT
)

if(NOT FRONTEND_RESULT EQUAL 0)
    message(FATAL_ERROR "zip parse failed:\n${FRONTEND_DIAGNOSTICS}")
endif()

file(SIZE "${OUTPUT_FILE}" AST_SIZE)
if(AST_SIZE EQUAL 0)
    message(FATAL_ERROR "zip parse produced no AST")
endif()
