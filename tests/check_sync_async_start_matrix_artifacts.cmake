if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED USER_SOURCE_FILE OR
   NOT DEFINED STD_SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, USER_SOURCE_FILE, STD_SOURCE_FILE and TARGET_MANIFEST are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

function(r_emit_hir source output_name)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${source}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "sync async-start HIR emit failed (${result}): ${error}")
    endif()
    set(${output_name} "${output}" PARENT_SCOPE)
endfunction()

function(r_emit_mir source output_name)
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${source}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "sync async-start MIR emit failed (${result}): ${error}")
    endif()
    set(${output_name} "${output}" PARENT_SCOPE)
endfunction()

# --all-functions: main calls none of the synchronous starters.
function(r_emit_ir source entry output_name)
    set(arguments
        --emit=llvm-ir --opt-level=0
        --all-functions
        --entry "${entry}"
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${source}"
    )
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" ${arguments}
        RESULT_VARIABLE first_result
        OUTPUT_VARIABLE first_output
        ERROR_VARIABLE first_error
    )
    if(NOT first_result EQUAL 0)
        message(FATAL_ERROR
            "sync async-start LLVM IR emit failed for ${source} (${first_result}): ${first_error}")
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" ${arguments}
        RESULT_VARIABLE second_result
        OUTPUT_VARIABLE second_output
        ERROR_VARIABLE second_error
    )
    if(NOT second_result EQUAL 0)
        message(FATAL_ERROR
            "repeated sync async-start LLVM IR emit failed (${second_result}): ${second_error}")
    endif()
    if(NOT first_output STREQUAL second_output)
        message(FATAL_ERROR "sync async-start LLVM IR output is not deterministic")
    endif()
    set(${output_name} "${first_output}" PARENT_SCOPE)
endfunction()

r_emit_hir("${USER_SOURCE_FILE}" user_hir)
r_emit_hir("${STD_SOURCE_FILE}" std_hir)
r_emit_mir("${USER_SOURCE_FILE}" user_mir)
r_emit_mir("${STD_SOURCE_FILE}" std_mir)

r_require_match_count(user_hir "\\(async_start " 2
    "sync callers of user async declarations")
r_require_match_count(user_hir "operation=std\\.async::cancel" 1
    "user async-start cancel resolution")
r_require_match_count(user_hir "operation=std\\.async::detach" 1
    "user async-start detach resolution")
r_require_match_count(user_hir "\\(local symbol=[0-9]+ name=\"retained_size\"" 1
    "user async-start failure must preserve its named Move operand")
r_require_match_count(user_hir "\\(result " 0
    "user async-start HIR must not recreate legacy result carriers")
r_require_match_count(user_mir " = async_start " 2
    "sync callers lower user async starts to MIR")
r_require_match_count(user_mir "async_staged=true" 1
    "user async-start MIR preserves its named Move operand until commit")
r_require_match_count(user_mir "\\(result " 0
    "user async-start MIR must not recreate legacy result carriers")

set(std_operations
    "std.fs::read_file"
    "std.fs::open_directory"
    "std.fs::create_directory_beneath"
    "std.fs::write_file_atomic_no_replace_beneath"
    "std.io::read"
    "std.io::write"
    "std.io::write_all"
    "std.io::write_shared"
    "std.io::close_input"
    "std.io::close_output"
    "std.io::flush"
)
foreach(operation IN LISTS std_operations)
    string(REPLACE "." "\\." operation_pattern "${operation}")
    r_require_match_count(std_hir "operation=${operation_pattern} " 1
        "sync standard async-start ${operation}")
    r_require_match_count(std_mir "operation=${operation_pattern} " 1
        "sync standard async-start MIR ${operation}")
endforeach()
r_require_match_count(std_hir "operation=std\\.async::cancel" 10
    "standard async-start cancel resolutions")
r_require_match_count(std_hir "operation=std\\.async::detach" 1
    "standard async-start detach resolution")
r_require_match_count(std_hir "\\(local symbol=[0-9]+ name=\"retained_size\"" 4
    "owned byte buffers remain usable on start failure")
r_require_match_count(std_hir "\\(result " 0
    "standard async-start HIR must not recreate legacy result carriers")
r_require_match_count(std_mir "async_staged=true" 7
    "standard async-start MIR preserves named Move operands until commit")
r_require_match_count(std_mir "\\(result " 0
    "standard async-start MIR must not recreate legacy result carriers")

r_emit_ir("${USER_SOURCE_FILE}" "test.codegen.sync_async_start_user::main" user_ir)
r_require_match_count(user_ir "call [^\n]*@r_std_async_cancel\\(" 1
    "user async-start cancel")
r_require_match_count(user_ir "call [^\n]*@r_std_async_detach\\(" 1
    "user async-start detach")
r_require_match_count(user_ir "@_?(setjmp|longjmp|sigsetjmp|siglongjmp)\\(" 0
    "user async-start checked transfer does not use non-local control transfer")

r_emit_ir("${STD_SOURCE_FILE}" "test.codegen.sync_async_start_std::main" std_ir)
r_require_match_count(std_ir "call [^\n]*@r_std_async_cancel\\(" 10
    "standard async-start cancellations")
r_require_match_count(std_ir "call [^\n]*@r_std_async_detach\\(" 1
    "standard async-start detach")
r_require_match_count(std_ir "@_?(setjmp|longjmp|sigsetjmp|siglongjmp)\\(" 0
    "standard async-start checked transfer does not use non-local control transfer")

# Each synchronous starter starts its operation with one call of the library entry.
set(native_operations
    "r_std_fs_read_file"
    "r_std_fs_open_directory"
    "r_std_fs_create_directory_beneath"
    "r_std_fs_write_file_atomic_no_replace_beneath"
    "r_std_io_read"
    "r_std_io_write"
    "r_std_io_write_all"
    "r_std_io_write_shared"
    "r_std_io_close_input"
    "r_std_io_close_output"
    "r_std_io_flush"
)
foreach(operation IN LISTS native_operations)
    r_require_match_count(std_ir "call [^\n]*@${operation}\\(" 1
        "${operation} call")
endforeach()
