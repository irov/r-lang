if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED SYNC_SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE, SYNC_SOURCE_FILE and TARGET_MANIFEST are required")
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
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${SOURCE_FILE}"
    RESULT_VARIABLE hir_result
    OUTPUT_VARIABLE hir_output
    ERROR_VARIABLE hir_error
)
if(NOT hir_result EQUAL 0)
    message(FATAL_ERROR "HIR emit failed (${hir_result}): ${hir_error}")
endif()
r_require_match_count(hir_output "operation=std\\.arc::clone" 2
    "HIR arc clone operations")
r_require_match_count(hir_output "operation=std\\.rc::clone" 2
    "HIR rc clone operations")
r_require_match_count(hir_output "operation=std\\.arc::clone type=\\(arc" 2
    "HIR source-inferred arc result types")
r_require_match_count(hir_output "operation=std\\.rc::clone type=\\(rc" 2
    "HIR source-inferred rc result types")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output "operation=std\\.arc::clone" 2
    "MIR arc clone operations")
r_require_match_count(mir_output "operation=std\\.rc::clone" 2
    "MIR rc clone operations")
r_require_match_count(mir_output "operation=std\\.arc::clone[^\n]*type=\\(arc" 2
    "MIR source-inferred arc result types")
r_require_match_count(mir_output "operation=std\\.rc::clone[^\n]*type=\\(rc" 2
    "MIR source-inferred rc result types")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen.shared_owner_clone::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_output
    ERROR_VARIABLE plan_error
)
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "link-plan emit failed (${plan_result}): ${plan_error}")
endif()
r_require_match_count(plan_output
    "\\(library module=\"std\\.arc\" target=\"r_std_arc\"\\)" 1
    "std.arc link-plan record")
r_require_match_count(plan_output
    "\\(library module=\"std\\.rc\" target=\"r_std_rc\"\\)" 1
    "std.rc link-plan record")

# Every clone operation is one call of its library entry. --all-functions lowers clone_sync,
# which main of the async program never calls.
foreach(mode IN ITEMS async sync)
    if(mode STREQUAL "async")
        set(source_file "${SOURCE_FILE}")
        set(clone_count 2)
    else()
        set(source_file "${SYNC_SOURCE_FILE}")
        set(clone_count 1)
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=llvm-ir --all-functions "${source_file}"
        RESULT_VARIABLE ir_result
        OUTPUT_VARIABLE ir_output
        ERROR_VARIABLE ir_error
    )
    if(NOT ir_result EQUAL 0)
        message(FATAL_ERROR "${mode} LLVM IR emit failed (${ir_result}): ${ir_error}")
    endif()
    r_require_match_count(ir_output "call [^\n]*@r_std_arc_clone\\(" ${clone_count}
        "${mode} std.arc clone calls")
    r_require_match_count(ir_output "call [^\n]*@r_std_rc_clone\\(" ${clone_count}
        "${mode} std.rc clone calls")
endforeach()
