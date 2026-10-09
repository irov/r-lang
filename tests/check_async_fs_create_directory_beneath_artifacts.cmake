if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED NEGATIVE_SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE, NEGATIVE_SOURCE_FILE and TARGET_MANIFEST are required")
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
    RESULT_VARIABLE hir_status
    OUTPUT_VARIABLE hir_output
    ERROR_VARIABLE hir_error
)
if(NOT hir_status EQUAL 0)
    message(FATAL_ERROR "HIR emit failed (${hir_status}): ${hir_error}")
endif()
r_require_match_count(hir_output
    "operation=std[.]fs::create_directory_beneath" 1
    "HIR create_directory_beneath operations")
r_require_match_count(hir_output
    "[(]local [^\n]*name=\"root_view\" type=[(]const_borrow [(]standard \"std[.]fs::directory\"[)][)]" 1
    "HIR named root borrows")
r_require_match_count(hir_output
    "[(]await [^\n]*type=void task=[(]task void [(]effects [(]standard \"std[.]fs::fs_error\"[)][)][)]" 1
    "HIR create_directory_beneath awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_status
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_status EQUAL 0)
    message(FATAL_ERROR "MIR emit failed (${mir_status}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "[(]local place=%local[0-9]+ name=\"root_view\" type=[(]const_borrow [(]standard \"std[.]fs::directory\"[)][)]" 1
    "MIR named root borrow storage")
r_require_match_count(mir_output
    "load place=%local[0-9]+ type=[(]const_borrow [(]standard \"std[.]fs::directory\"[)][)] borrow_origin=%local[0-9]+" 1
    "MIR named root borrow operand")
r_require_match_count(mir_output
    "borrow place=%local[0-9]+ type=[(]const_borrow [(]standard \"std[.]fs::path\"[)][)] borrow_origin=%local[0-9]+" 1
    "MIR relative path borrow operand")
r_require_match_count(mir_output
    "constant type=bool value=1" 1
    "MIR recursive operand")
r_require_match_count(mir_output
    "%v[0-9]+ = variant type=[(]option [(]standard \"std[.]time::instant\"[)][)] tag=0" 2
    "MIR deadline operands")
r_require_match_count(mir_output
    "standard_call operation=std[.]fs::create_directory_beneath root=%v[0-9]+ relative=%v[0-9]+ recursive=%v[0-9]+ deadline=%v[0-9]+ call_bounded_borrows=[(]root relative[)]" 1
    "MIR call-bounded create_directory_beneath operations")
r_require_match_count(mir_output
    "await [^\n]*task=[(]task void [(]effects [(]standard \"std[.]fs::fs_error\"[)][)][)] type=[(]carrier void [(]effects [(]standard \"std[.]fs::fs_error\"[)][)][)]" 1
    "MIR create_directory_beneath awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen.async_fs_create_directory_beneath::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE plan_status
    OUTPUT_VARIABLE plan_output
    ERROR_VARIABLE plan_error
)
if(NOT plan_status EQUAL 0)
    message(FATAL_ERROR "link-plan emit failed (${plan_status}): ${plan_error}")
endif()
r_require_match_count(plan_output
    "[(]library module=\"std[.]fs\" target=\"r_std_fs\"[)]" 1
    "std.fs link-plan record")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --entry test.codegen.async_fs_create_directory_beneath::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE ir_status
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_status EQUAL 0)
    message(FATAL_ERROR "LLVM IR emit failed (${ir_status}): ${ir_error}")
endif()
r_require_match_count(ir_output
    "call [^\n]*@r_std_fs_create_directory_beneath[(]" 1
    "create_directory_beneath library calls")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${NEGATIVE_SOURCE_FILE}"
    RESULT_VARIABLE negative_status
    OUTPUT_VARIABLE negative_output
    ERROR_VARIABLE negative_error
)
if(NOT negative_status EQUAL 1)
    message(FATAL_ERROR
        "invalid create_directory_beneath arity returned ${negative_status}: "
        "${negative_output}${negative_error}")
endif()
r_require_match_count(negative_error
    "R-DIAG-TYPE-001 [^\n]*R-SLIB-FS-0008" 1
    "create_directory_beneath arity diagnostics")
