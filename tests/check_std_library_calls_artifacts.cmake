if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR "missing standard library artifact test input")
endif()

function(capture_plan output_variable)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}"
            --emit=link-plan
            --entry audit.async_std_library_calls::main
            --target-manifest "${TARGET_MANIFEST}"
            "${SOURCE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "standard library link-plan failed (${result}): ${error}")
    endif()
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

capture_plan(first_plan)
capture_plan(second_plan)
if(NOT first_plan STREQUAL second_plan)
    message(FATAL_ERROR "standard library link-plan is not deterministic")
endif()

foreach(module IN ITEMS alloc bytes c math string time)
    string(REGEX MATCHALL
        "\\(library module=\"std[.]${module}\" target=\"r_std_${module}\"\\)"
        records
        "${first_plan}"
    )
    list(LENGTH records record_count)
    if(NOT record_count EQUAL 1)
        message(FATAL_ERROR
            "expected one std.${module} link-plan record, found ${record_count}:\n${first_plan}")
    endif()
endforeach()
