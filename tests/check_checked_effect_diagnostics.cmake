if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED CASE_COUNT)
    message(FATAL_ERROR "R_FRONT_EXECUTABLE and CASE_COUNT are required")
endif()

foreach(case_index RANGE 1 ${CASE_COUNT})
    set(source_variable "SOURCE_${case_index}")
    set(rule_variable "RULE_${case_index}")
    if(NOT DEFINED ${source_variable} OR NOT DEFINED ${rule_variable})
        message(FATAL_ERROR "${source_variable} and ${rule_variable} are required")
    endif()

    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${${source_variable}}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 1)
        message(FATAL_ERROR
            "${${source_variable}} returned ${status}, expected source diagnostic:\n"
            "${output}${error}")
    endif()
    string(REGEX MATCHALL "${${rule_variable}}" matches "${error}")
    list(LENGTH matches match_count)
    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "${${source_variable}} expected one ${${rule_variable}}, found ${match_count}:\n"
            "${error}")
    endif()
endforeach()
