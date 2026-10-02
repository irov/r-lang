if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR "R_FRONT_EXECUTABLE, SOURCE_FILE and TARGET_MANIFEST are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

function(r_require_text variable expected description)
    string(FIND "${${variable}}" "${expected}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${description}: exact text not found:\n${${variable}}")
    endif()
endfunction()

function(r_capture_artifact emit_kind output_variable)
    set(arguments --emit=${emit_kind})
    if(emit_kind STREQUAL "c17" OR emit_kind STREQUAL "link-plan")
        list(APPEND arguments
            --entry "test.codegen.fs_path_is_absolute::main"
            --profile hosted-native-async
            --target-manifest "${TARGET_MANIFEST}")
    endif()
    list(APPEND arguments "${SOURCE_FILE}")
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" ${arguments}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${emit_kind} emit failed (${result}): ${error}")
    endif()
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

function(r_require_deterministic first second description)
    if(NOT "${${first}}" STREQUAL "${${second}}")
        message(FATAL_ERROR "${description} is not deterministic")
    endif()
endfunction()

r_capture_artifact(hir hir_output)
r_capture_artifact(hir hir_repeated)
r_require_deterministic(hir_output hir_repeated "std.fs::path_is_absolute HIR artifact")
r_require_match_count(hir_output "operation=std[.]fs::path_is_absolute" 4
    "HIR std.fs::path_is_absolute operations")
r_require_match_count(hir_output
    "standard_call operation=std[.]fs::path_is_absolute type=bool input=[(]const_borrow [(]standard \"std[.]fs::path\"[)][)]"
    4
    "HIR exact path borrow and bool result")
r_require_match_count(hir_output
    "borrow type=[(]const_borrow [(]standard \"std[.]fs::path\"[)][)]"
    4
    "HIR shared path borrows including the helper call")

r_capture_artifact(mir mir_output)
r_capture_artifact(mir mir_repeated)
r_require_deterministic(mir_output mir_repeated "std.fs::path_is_absolute MIR artifact")
r_require_match_count(mir_output "operation=std[.]fs::path_is_absolute" 4
    "MIR std.fs::path_is_absolute operations")
r_require_match_count(mir_output
    "standard_call operation=std[.]fs::path_is_absolute source=%v[0-9]+ type=bool input=[(]const_borrow [(]standard \"std[.]fs::path\"[)][)]"
    4
    "MIR exact borrowed sources and bool results")
r_require_match_count(mir_output
    "borrow place=%local[0-9]+ type=[(]const_borrow [(]standard \"std[.]fs::path\"[)][)]"
    4
    "MIR local path borrows including the helper call")

r_capture_artifact(c17 c17_output)
r_capture_artifact(c17 c17_repeated)
r_require_deterministic(c17_output c17_repeated "std.fs::path_is_absolute C17 artifact")
r_require_match_count(c17_output "#include \"r_std_fs[.]h\"" 1
    "generated std.fs include")
r_require_match_count(c17_output "r_std_fs_path_is_absolute[(]" 4
    "generated native path observations")
r_require_text(c17_output "typedef const RStdFsPath *" "generated shared path borrow ABI")
r_require_text(c17_output "_Bool" "generated bool ABI")

r_capture_artifact(link-plan plan_output)
r_capture_artifact(link-plan plan_repeated)
r_require_deterministic(plan_output plan_repeated
    "std.fs::path_is_absolute link-plan artifact")
r_require_match_count(plan_output
    "[(]library module=\"std[.]fs\" target=\"r_std_fs\"[)]"
    1
    "std.fs link-plan records")
