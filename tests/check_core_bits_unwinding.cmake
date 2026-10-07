# Library R-LIB-0027 (L45): the bit and wide integer helpers never run R code, so no panic test
# follows a call of one in synchronous or asynchronous C (defect L45-4).
if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED SOURCES)
    message(FATAL_ERROR "core bits unwinding test arguments are missing")
endif()

foreach(source IN LISTS SOURCES)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=c17 "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE generated ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "core bits unwinding: ${source} failed: ${errors}")
    endif()
    string(FIND "${generated}" "r_core_widening_mul_u64(" helper)
    if(helper EQUAL -1)
        message(FATAL_ERROR "core bits unwinding: ${source} calls no wide helper")
    endif()
    string(REGEX MATCH "r_core_[a-z_]+_[iu][a-z0-9]*\\([^;]*\\);\n[ ]*if \\(r_runtime_unwinding\\(\\)\\)"
           tested "${generated}")
    if(tested)
        message(FATAL_ERROR "core bits unwinding: a panic test follows ${tested}")
    endif()
endforeach()
