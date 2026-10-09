if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST OR
   NOT DEFINED CASE)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE, TARGET_MANIFEST and CASE are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

if(CASE STREQUAL "read_file")
    set(deadline_count 1)
    set(entry "test.codegen.async_fs_read_file::main")
    set(operation "read_file")
    set(hir_value "[(]array u8[)]")
    set(mir_call
        "standard_call operation=std[.]fs::read_file path=%local[0-9]+ limit=%v[0-9]+ deadline=%v[0-9]+")
    set(native_call "@r_std_fs_read_file[(]")
elseif(CASE STREQUAL "open_directory")
    set(deadline_count 1)
    set(entry "test.codegen.async_fs_open_directory::main")
    set(operation "open_directory")
    set(hir_value "[(]standard \"std[.]fs::directory\"[)]")
    set(mir_call
        "standard_call operation=std[.]fs::open_directory path=%local[0-9]+ deadline=%v[0-9]+")
    set(native_call "@r_std_fs_open_directory[(]")
elseif(CASE STREQUAL "file_metadata")
    set(deadline_count 2)
    set(entry "test.codegen.async_fs_file_metadata::main")
    set(operation "file_metadata")
    set(hir_value "[(]standard \"std[.]fs::metadata\"[)]")
    set(mir_call "standard_call operation=std[.]fs::file_metadata [^\n]+")
    set(native_call "@r_std_fs_file_metadata[(]")
else()
    message(FATAL_ERROR "unknown async filesystem artifact CASE: ${CASE}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${SOURCE_FILE}"
    RESULT_VARIABLE hir_status
    OUTPUT_VARIABLE hir_output
    ERROR_VARIABLE hir_error
)
if(NOT hir_status EQUAL 0)
    message(FATAL_ERROR "HIR emit failed (${hir_status}): ${hir_error}")
endif()
r_require_match_count(hir_output "operation=std[.]fs::${operation}" 1
    "HIR ${operation} operations")
r_require_match_count(hir_output
    "[(]await [^\n]*type=${hir_value} task=[(]task ${hir_value} [(]effects [(]standard \"std[.]fs::fs_error\"[)][)][)]" 1
    "HIR ${operation} awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_status
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_status EQUAL 0)
    message(FATAL_ERROR "MIR emit failed (${mir_status}): ${mir_error}")
endif()
r_require_match_count(mir_output "${mir_call}" 1
    "MIR call-bounded ${operation} operations")
r_require_match_count(mir_output
    "await [^\n]*task=[(]task ${hir_value} [(]effects [(]standard \"std[.]fs::fs_error\"[)][)][)] type=[(]carrier ${hir_value} [(]effects [(]standard \"std[.]fs::fs_error\"[)][)][)]" 1
    "MIR ${operation} awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --entry "${entry}"
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
r_require_match_count(ir_output "call [^\n]*@r_std_fs_path_from_utf8[(]" 1
    "path_from_utf8 calls")
r_require_match_count(ir_output "call [^\n]*${native_call}" 1
    "${operation} calls")
# R-STMT-0019 (L23): every filesystem start narrows its deadline argument by the structural
# deadline of the starting task.
r_require_match_count(ir_output "call [^\n]*@r_runtime_task_deadline_narrow[(]" ${deadline_count}
    "filesystem deadline narrowing")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry "${entry}"
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
