# R-AGG-0013 (L42): interface schema 34 exports the targets of an attribute type and the
# attributes of the program on each type, field and enumerator, with their argument values.
if(NOT DEFINED R_FRONT_EXECUTABLE OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "attribute interface test arguments are missing")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=interface "${SOURCE}"
    RESULT_VARIABLE status OUTPUT_VARIABLE interface ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "attribute interface failed: ${errors}")
endif()
foreach(expected IN ITEMS
        "(interface version=34"
        "test.codegen.user_attributes::table\" kind=struct"
        "attribute_targets=(type)"
        "attribute_targets=(field)"
        "attribute_targets=(variant)"
        "attributes=((type=(struct \"test.codegen.user_attributes\"::\"table\") values=(\"players\")))"
        "attributes=((type=(struct \"test.codegen.user_attributes\"::\"key\") values=()))"
        "values=(\"nick\" 32 1 1 13836183955189006336 110 7)"
        "attributes=((type=(struct \"test.codegen.user_attributes\"::\"label\") values=(\"Blue\")))")
    string(FIND "${interface}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "attribute interface is missing ${expected}")
    endif()
endforeach()
