# R-TYPE-0043, R-STMT-0023 (L44): interface schema 34 writes a fields constraint as
# `(fields Trait)`, and the constant of a translation-time loop is no constant of the module.
if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "fields interface test arguments are missing")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=interface "${SOURCE}"
    RESULT_VARIABLE status OUTPUT_VARIABLE interface ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "fields interface failed: ${errors}")
endif()
foreach(expected IN ITEMS
        "(interface version=34"
        "(fields (name=\"Weigh\" module=\"test.codegen.generic_field_access\" arguments=()))"
        "(fields (name=\"Grow\" module=\"test.codegen.generic_field_access\" arguments=()))")
    string(FIND "${interface}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "fields interface is missing ${expected}")
    endif()
endforeach()
string(FIND "${interface}" "(constant name=" found)
if(NOT found EQUAL -1)
    message(FATAL_ERROR "fields interface exports a loop constant")
endif()
