# Library R-LIB-0027 (L45): the bit and wide integer helpers never run R code, so they lower to
# plain integer operations and no panic test follows one in synchronous or asynchronous
# programs (defect L45-4).
if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED SOURCES)
    message(FATAL_ERROR "core bits unwinding test arguments are missing")
endif()

# --all-functions: main calls the check functions with constant arguments, so they are evaluated
# at translation (Core R-FUNC-0023) and lowered only on request.
foreach(source IN LISTS SOURCES)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --opt-level=0 --all-functions "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE generated ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "core bits unwinding: ${source} failed: ${errors}")
    endif()
    # core::widening_mul_u64 is a double-width product, not a call.
    string(FIND "${generated}" "mul i128 " wide)
    if(wide EQUAL -1)
        message(FATAL_ERROR "core bits unwinding: ${source} has no wide multiplication")
    endif()
    string(REGEX MATCH "call [^\n]*@r_core_[a-z_]+_[iu][a-z0-9]*\\(" called "${generated}")
    if(called)
        message(FATAL_ERROR "core bits unwinding: a helper is called out of line: ${called}")
    endif()
    string(REGEX MATCH
           "call [^\n]*@llvm\\.(ctpop|ctlz|cttz|bswap|fshl|fshr)\\.[^\n]*\n[^\n]*@r_runtime_unwinding"
           tested "${generated}")
    if(tested)
        message(FATAL_ERROR "core bits unwinding: a panic test follows ${tested}")
    endif()
endforeach()
