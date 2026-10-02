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
        --emit=c17
        --entry test.codegen.async_fs_create_directory_beneath::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_status
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_status EQUAL 0)
    message(FATAL_ERROR "C17 emit failed (${c17_status}): ${c17_error}")
endif()
r_require_match_count(c17_output
    "#include \"r_std_fs[.]h\"" 1
    "C17 std.fs include")
r_require_match_count(c17_output
    "RStdFsVoidResult canonical result ABI" 1
    "C17 void-result await ABI assertion")
r_require_match_count(c17_output
    "r_d00000006 r_v00000052" 1
    "C17 checked void-completion carrier")
r_require_match_count(c17_output
    "RStdAsyncStartError r_v00000050" 1
    "C17 start error value")
r_require_match_count(c17_output
    "RStdFsError r_v00000056" 1
    "C17 completion error value")
r_require_match_count(c17_output
    "r_d00000003 r_stack_r_l00000008 = [{]0[}]" 1
    "C17 transient named root borrow storage")
r_require_match_count(c17_output
    "r_d00000003 r_stack_r_v0000004[12] = [{]0[}]" 2
    "C17 transient root borrow values")
r_require_match_count(c17_output
    "r_d00000002 r_stack_r_v00000043 = [{]0[}]" 1
    "C17 transient path borrow value")
r_require_match_count(c17_output
    "frame->r_l00000008" 0
    "C17 frame root borrow storage")
r_require_match_count(c17_output
    "frame->r_v0000004[123]" 0
    "C17 frame call-bounded borrow values")
r_require_match_count(c17_output
    "r_std_fs_create_directory_beneath[(]r_stack_r_v00000042,[\n ]+r_stack_r_v00000043,[\n ]+frame->r_v00000044,[\n ]+r_fs_deadline_00000047[)]" 1
    "C17 exact create_directory_beneath native call")
r_require_match_count(c17_output
    "R_INTERNAL_ASSERT[(]" 0
    "C17 omits assertion infrastructure")
r_require_match_count(c17_output
    "if [(]frame->r_v00000045[.]r_tag > UINT32_C[(]1[)][)]" 0
    "C17 deadline option runtime guards")
r_require_match_count(c17_output
    "if [(]frame->r_v00000045[.]r_tag == UINT32_C[(]1[)][)]" 1
    "C17 deadline option mapping")
r_require_match_count(c17_output
    "RStdFsTaskStartResult r_fs_start_00000047 =" 1
    "C17 native task start result")
r_require_match_count(c17_output
    "r_runtime_assert[.]h" 0
    "C17 omits assertion headers")
r_require_match_count(c17_output
    "r_fs_start_00000047[.]error == R_STD_ASYNC_START_ALLOCATION_FAILED" 0
    "C17 omits allocation start-error assertions")
r_require_match_count(c17_output
    "r_fs_start_00000047[.]error == R_STD_ASYNC_START_RUNTIME_STOPPING" 0
    "C17 omits runtime-stopping start-error assertions")

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
