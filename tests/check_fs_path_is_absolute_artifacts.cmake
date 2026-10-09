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

function(r_capture_artifact emit_kind output_variable)
    set(arguments --emit=${emit_kind})
    if(emit_kind STREQUAL "llvm-ir" OR emit_kind STREQUAL "link-plan")
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

r_capture_artifact(llvm-ir ir_output)
r_capture_artifact(llvm-ir ir_repeated)
r_require_deterministic(ir_output ir_repeated "std.fs::path_is_absolute LLVM IR artifact")
# Each observation passes the borrowed path by address and receives a bool.
r_require_match_count(ir_output "call zeroext i1 @r_std_fs_path_is_absolute[(]ptr [^)]+[)]" 4
    "library path observations")

r_capture_artifact(link-plan plan_output)
r_capture_artifact(link-plan plan_repeated)
r_require_deterministic(plan_output plan_repeated
    "std.fs::path_is_absolute link-plan artifact")
r_require_match_count(plan_output
    "[(]library module=\"std[.]fs\" target=\"r_std_fs\"[)]"
    1
    "std.fs link-plan records")
