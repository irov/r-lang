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

function(r_require_present variable pattern description)
    string(REGEX MATCH "${pattern}" match "${${variable}}")
    if(match STREQUAL "")
        message(FATAL_ERROR "${description}: '${pattern}' is absent:\n${${variable}}")
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
        --emit=llvm-ir
        --entry test.codegen.async_fs_write_file_atomic_no_replace_beneath::main
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
    "call [^\n]*@r_std_fs_write_file_atomic_no_replace_beneath[(]" 1
    "atomic-write-beneath library calls")
# open_directory, read_file and the atomic write are the three awaited filesystem tasks.
r_require_match_count(ir_output
    "call [^\n]*@r_runtime_task_execution_await[(]" 3
    "filesystem task awaits")
# The move and drop glue of std.fs::write_file_result reach the library's own entries.
r_require_present(ir_output
    "call [^\n]*@(r_shim_)?r_std_fs_write_file_result_move_initialize[(]"
    "write_file_result move glue")
r_require_present(ir_output
    "call [^\n]*@(r_shim_)?r_std_fs_write_file_result_destroy[(]"
    "write_file_result drop glue")
