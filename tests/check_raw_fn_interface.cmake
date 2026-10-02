if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED SOURCE_MAIN OR NOT DEFINED SOURCE_LIBRARY)
    message(FATAL_ERROR "raw function interface test arguments are missing")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=interface "${SOURCE_MAIN}" "${SOURCE_LIBRARY}"
    RESULT_VARIABLE first_status OUTPUT_VARIABLE first ERROR_VARIABLE first_error)
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=interface "${SOURCE_LIBRARY}" "${SOURCE_MAIN}"
    RESULT_VARIABLE second_status OUTPUT_VARIABLE second ERROR_VARIABLE second_error)
if(NOT first_status EQUAL 0 OR NOT second_status EQUAL 0)
    message(FATAL_ERROR "raw function interface failed: ${first_error}${second_error}")
endif()
if(NOT first STREQUAL second)
    message(FATAL_ERROR "raw function interface changes when module inputs are reordered")
endif()
foreach(expected IN ITEMS
        "(interface version=31"
        "(raw_fn parameters=(c_int) return=c_int)"
        "callback=true c_name=\"r_audit_module_increment\""
        "audit.std_c_callback_module::Box"
        "audit.std_c_callback_module::identity")
    string(FIND "${first}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "raw function interface is missing ${expected}")
    endif()
endforeach()
