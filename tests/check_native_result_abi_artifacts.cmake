if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "R_FRONT_EXECUTABLE and SOURCE_FILE are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

# --all-functions: main calls none of the checked functions.
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --all-functions
        --entry test.codegen_native_result_abi::main
        "${SOURCE_FILE}"
    RESULT_VARIABLE ir_result
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR "LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()

# Checked R-to-R functions keep the program's own calling convention: the outcome is written to
# the carrier the caller passes first, and the result types of the library operations
# (std.fs/std.io *_result) are not reused for them.
r_require_match_count(ir_output
    "define internal void @\"test[.]codegen_native_result_abi::pass_[a-z_]+\"[(]ptr %0"
    4 "checked functions use explicit output carriers")
r_require_match_count(ir_output "@(r_shim_)?r_std_(fs|io)_[a-z_]*result" 0
    "checked R-to-R functions do not reuse library result types")
