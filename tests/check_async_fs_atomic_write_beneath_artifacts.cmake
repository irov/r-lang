if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE and TARGET_MANIFEST are required")
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
    "operation=std[.]fs::write_file_atomic_no_replace_beneath" 1
    "HIR atomic-write-beneath operations")
r_require_match_count(hir_output
    "[(]borrow type=[(]const_borrow [(]standard \"std[.]fs::directory\"[)][)][\n ]+[(]deref_place type=[(]standard \"std[.]fs::directory\"[)]" 1
    "HIR transient root borrows")
r_require_match_count(hir_output
    "[(]await [^\n]*type=[(]standard \"std[.]fs::write_file_result\"[)]" 1
    "HIR atomic-write-beneath awaits")

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
    "move source=%local[0-9]+ type=[(]array u8[)] async_staged=true" 1
    "MIR staged data moves")
r_require_match_count(mir_output
    "standard_call operation=std[.]fs::write_file_atomic_no_replace_beneath root=%v[0-9]+ relative=%v[0-9]+ data=%v[0-9]+ deadline=%v[0-9]+ call_bounded_borrows=[(]root relative[)] staged_moves=[(]data[)]" 1
    "MIR atomic-write-beneath operation shape")
r_require_match_count(mir_output
    "await [^\n]*type=[(]standard \"std[.]fs::write_file_result\"[)]" 1
    "MIR atomic-write-beneath awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen.async_fs_write_file_atomic_no_replace_beneath::main
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
        --entry test.codegen.async_fs_write_file_atomic_no_replace_beneath::main
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
    "C17 std.fs includes")
r_require_match_count(c17_output
    "r_std_fs_write_file_atomic_no_replace_beneath" 1
    "C17 atomic-write-beneath native calls")
r_require_match_count(c17_output
    "r_d[0-9]+ r_stack_r_v00000065 = [{]0[}]" 1
    "C17 transient root borrow storage")
r_require_match_count(c17_output
    "r_d[0-9]+ r_stack_r_v00000066 = [{]0[}]" 1
    "C17 transient relative borrow storage")
r_require_match_count(c17_output
    "frame->r_v00000065" 0
    "C17 escaped root borrow frame fields")
r_require_match_count(c17_output
    "frame->r_v00000066" 0
    "C17 escaped relative borrow frame fields")
r_require_match_count(c17_output
    "r_std_fs_write_file_atomic_no_replace_beneath[(]r_stack_r_v[0-9]+,[\n ]+r_stack_r_v[0-9]+,[\n ]+&frame->r_l[0-9]+,[\n ]+r_fs_deadline_[0-9]+[)]" 1
    "C17 exact native atomic-write operands")
r_require_match_count(c17_output
    "const RRuntimeArray r_fs_data_before_00000070 = frame->r_l00000013" 0
    "C17 omits debug staged-data snapshots")
r_require_match_count(c17_output
    "if [(]r_fs_start_00000070[.]is_ok[)] [{]" 2
    "C17 native success and result adaptation branches")
r_require_match_count(c17_output
    "frame->r_l00000013_initialized = 0" 3
    "C17 conditional consume and terminal data cleanup transitions")
r_require_match_count(c17_output
    "r_fs_start_00000070[.]error == R_STD_ASYNC_START_ALLOCATION_FAILED" 0
    "C17 omits allocation-failure assertions")
r_require_match_count(c17_output
    "r_fs_start_00000070[.]error == R_STD_ASYNC_START_RUNTIME_STOPPING" 0
    "C17 omits runtime-stopping assertions")
r_require_match_count(c17_output
    "r_fs_data_before_00000070[.]data" 0
    "C17 omits start-failure data-owner snapshots")
r_require_match_count(c17_output
    "r_fs_data_before_00000070[.]length" 0
    "C17 omits start-failure data-length snapshots")
r_require_match_count(c17_output
    "r_fs_data_before_00000070[.]capacity" 0
    "C17 omits start-failure data-capacity snapshots")
r_require_match_count(c17_output "R_INTERNAL_ASSERT[(]" 0
    "C17 omits assertions")
r_require_match_count(c17_output
    "r_runtime_task_execution_await[(]execution, &frame->r_l[0-9]+, &frame->r_v[0-9]+[)]" 3
    "C17 filesystem await ABI calls")
r_require_match_count(c17_output
    "RStdFsWriteFileResult r_v00000076" 1
    "C17 write_file_result await storage")
r_require_match_count(c17_output
    "r_std_fs_write_file_result_move_initialize" 1
    "C17 write_file_result move adapter")
r_require_match_count(c17_output
    "r_std_fs_write_file_result_destroy" 1
    "C17 write_file_result drop adapter")
r_require_match_count(c17_output
    "R_STD_FS_WRITE_FILE_COMMITTED" 0
    "C17 omits assertion-only committed-tag references")
r_require_match_count(c17_output
    "R_STD_FS_WRITE_FILE_FAILED" 0
    "C17 omits assertion-only failed-tag references")
r_require_match_count(c17_output
    "frame->r_v00000077 = [(]uint32_t[)]frame->r_v00000076[.]kind" 1
    "C17 directly discriminates the write-file outcome")
r_require_match_count(c17_output
    "R_STD_FS_WRITE_FILE_RESULT_" 0
    "C17 non-ABI filesystem outcome tags")
r_require_match_count(c17_output
    "[(]RStdFsErrorCode[)]INT32_C[(]4[)]" 1
    "C17 already_exists ABI value")
