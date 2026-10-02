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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=c17 "${SOURCE_FILE}"
    RESULT_VARIABLE async_c17_result
    OUTPUT_VARIABLE async_c17_output
    ERROR_VARIABLE async_c17_error
)
if(NOT async_c17_result EQUAL 0)
    message(FATAL_ERROR
        "async C17 emit failed (${async_c17_result}): ${async_c17_error}")
endif()
r_require_match_count(async_c17_output "#include \"r_std_arc\\.h\"" 1
    "async generated std.arc include")
r_require_match_count(async_c17_output "#include \"r_std_rc\\.h\"" 1
    "async generated std.rc include")
r_require_match_count(async_c17_output "r_std_arc_clone\\(" 1
    "async generated std.arc clone bridge")
r_require_match_count(async_c17_output "r_std_rc_clone\\(" 1
    "async generated std.rc clone bridge")
r_require_match_count(async_c17_output "r_clone_arc\\(" 2
    "async generated arc bridge definition and call site")
r_require_match_count(async_c17_output "r_clone_rc\\(" 2
    "async generated rc bridge definition and call site")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=c17 "${SYNC_SOURCE_FILE}"
    RESULT_VARIABLE sync_c17_result
    OUTPUT_VARIABLE sync_c17_output
    ERROR_VARIABLE sync_c17_error
)
if(NOT sync_c17_result EQUAL 0)
    message(FATAL_ERROR
        "sync C17 emit failed (${sync_c17_result}): ${sync_c17_error}")
endif()
r_require_match_count(sync_c17_output "#include \"r_std_arc\\.h\"" 1
    "sync generated std.arc include")
r_require_match_count(sync_c17_output "#include \"r_std_rc\\.h\"" 1
    "sync generated std.rc include")
r_require_match_count(sync_c17_output "r_std_arc_clone\\(" 1
    "sync generated std.arc clone bridge")
r_require_match_count(sync_c17_output "r_std_rc_clone\\(" 1
    "sync generated std.rc clone bridge")
r_require_match_count(sync_c17_output "r_clone_arc\\(" 2
    "sync generated arc bridge definition and call site")
r_require_match_count(sync_c17_output "r_clone_rc\\(" 2
    "sync generated rc bridge definition and call site")
