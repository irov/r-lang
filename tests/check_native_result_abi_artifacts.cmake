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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen_native_result_abi::main
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "C17 emit failed (${c17_result}): ${c17_error}")
endif()

r_require_match_count(c17_output "#include \"r_std_fs[.]h\"" 1
    "generated std.fs include")
r_require_match_count(c17_output "#include \"r_std_io[.]h\"" 1
    "generated std.io include")

foreach(native_type IN ITEMS RStdFsArrayResult RStdFsDirectoryResult RStdFsVoidResult RStdIoVoidResult)
    r_require_match_count(c17_output
        "${native_type}"
        0
        "checked R-to-R functions do not reuse ${native_type}")
endforeach()

r_require_match_count(c17_output "struct r_d[0-9]+ [{]" 5
    "one derived checked carrier per distinct signature")
r_require_match_count(c17_output "RRuntimeArray r_ok" 1
    "array checked success payload")
r_require_match_count(c17_output "RStdFsDirectory r_ok" 1
    "directory checked success payload")
r_require_match_count(c17_output "RStdFsError r_error_00000001" 3
    "filesystem checked error payloads")
r_require_match_count(c17_output "RStdIoError r_error_00000001" 1
    "I/O checked error payload")
r_require_match_count(c17_output
    "void r_f[0-9]+[(]r_d[0-9]+ [*]r_effect_out"
    5 "checked functions use explicit output carriers")
r_require_match_count(c17_output
    "[*]r_effect_out = [(]r_d[0-9]+[)][{]0[}]"
    0 "checked output carriers do not materialize whole-carrier zero temporaries")
r_require_match_count(c17_output
    "r_type_move_[a-zA-Z0-9_]+[(]&r_effect_out->r_payload[.]r_ok, &r_t[0-9]+[)]"
    2 "Move success payloads are initialized directly in checked output carriers")
r_require_match_count(c17_output
    "r_effect_out->r_tag = UINT32_C[(]0[)]"
    5 "checked functions publish the success tag exactly once")
