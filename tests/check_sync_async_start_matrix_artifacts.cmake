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

function(r_emit_c17 source entry output_name)
    set(arguments
        --emit=c17
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
        message(FATAL_ERROR "sync async-start C17 emit failed (${first_result}): ${first_error}")
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}" ${arguments}
        RESULT_VARIABLE second_result
        OUTPUT_VARIABLE second_output
        ERROR_VARIABLE second_error
    )
    if(NOT second_result EQUAL 0)
        message(FATAL_ERROR
            "repeated sync async-start C17 emit failed (${second_result}): ${second_error}")
    endif()
    if(NOT first_output STREQUAL second_output)
        message(FATAL_ERROR "sync async-start C17 output is not deterministic")
    endif()
    set(${output_name} "${first_output}" PARENT_SCOPE)
endfunction()

# A start implementation may branch directly instead of materializing a local carrier. If it
# materializes one, however, its selected payload must precede the corresponding published tag.
function(r_require_optional_payload_before_tag variable call_name description)
    string(FIND "${${variable}}" "${call_name}(" call_offset)
    if(call_offset EQUAL -1)
        message(FATAL_ERROR "${description}: native start call is absent")
    endif()
    string(LENGTH "${${variable}}" output_length)
    math(EXPR remaining "${output_length} - ${call_offset}")
    if(remaining GREATER 2400)
        set(remaining 2400)
    endif()
    string(SUBSTRING "${${variable}}" ${call_offset} ${remaining} tail)

    string(FIND "${tail}" ".r_payload.r_ok" success_payload)
    string(FIND "${tail}" ".r_tag = UINT32_C(0);" success_tag)
    if(NOT success_tag EQUAL -1 AND
       (success_payload EQUAL -1 OR NOT success_payload LESS success_tag))
        message(FATAL_ERROR "${description}: success tag precedes its payload:\n${tail}")
    endif()

    string(FIND "${tail}" ".r_payload.r_error_00000001" error_payload)
    string(FIND "${tail}" ".r_tag = UINT32_C(1);" error_tag)
    if(NOT error_tag EQUAL -1 AND
       (error_payload EQUAL -1 OR NOT error_payload LESS error_tag))
        message(FATAL_ERROR "${description}: error tag precedes its payload:\n${tail}")
    endif()
endfunction()

# Named Move operands must be passed to the native two-phase start while still owned by the
# caller. The first status observation must therefore occur before generated catch cleanup.
function(r_require_status_before_cleanup variable call_name description)
    string(FIND "${${variable}}" "${call_name}(" call_offset)
    if(call_offset EQUAL -1)
        message(FATAL_ERROR "${description}: native start call is absent")
    endif()
    string(SUBSTRING "${${variable}}" 0 ${call_offset} prefix)
    string(FIND "${prefix}" "\nvoid r_f" function_offset REVERSE)
    if(function_offset EQUAL -1)
        message(FATAL_ERROR "${description}: containing generated function is absent")
    endif()
    string(SUBSTRING "${prefix}" ${function_offset} -1 before_call)
    string(REGEX MATCH "r_type_(move|drop)_[A-Za-z0-9_]+\\(" premature_transfer
        "${before_call}")
    if(premature_transfer)
        message(FATAL_ERROR
            "${description}: named Move operand was transferred before native start:\n"
            "${before_call}")
    endif()
    string(LENGTH "${${variable}}" output_length)
    math(EXPR remaining "${output_length} - ${call_offset}")
    if(remaining GREATER 2400)
        set(remaining 2400)
    endif()
    string(SUBSTRING "${${variable}}" ${call_offset} ${remaining} tail)
    string(FIND "${tail}" ".is_ok" status_offset)
    string(FIND "${tail}" "r_type_drop_" cleanup_offset)
    if(status_offset EQUAL -1 OR
       (NOT cleanup_offset EQUAL -1 AND NOT status_offset LESS cleanup_offset))
        message(FATAL_ERROR
            "${description}: source cleanup is not guarded by the native start status:\n${tail}")
    endif()
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

r_emit_c17("${USER_SOURCE_FILE}" "test.codegen.sync_async_start_user::main" user_c17)
r_emit_c17("${STD_SOURCE_FILE}" "test.codegen.sync_async_start_std::main" std_c17)

r_require_match_count(user_c17 "r_std_async_cancel\\(" 1
    "generated user async-start cancel")
r_require_match_count(user_c17 "r_std_async_detach\\(" 1
    "generated user async-start detach")
r_require_match_count(user_c17
    "static void r_f[0-9]+\\(r_d[0-9]+ \\*r_effect_out" 2
    "generated user async wrappers use explicit output carriers")
r_require_match_count(user_c17 "static r_d[0-9]+ r_f[0-9]+\\(" 0
    "generated user async wrappers must not return carriers by value")
r_require_match_count(user_c17 "static r_d[0-9]+ r_async_launch_[0-9]+\\(" 0
    "generated async launch helpers must not return carriers by value")
r_require_match_count(std_c17 "r_std_async_cancel\\(" 10
    "generated standard async-start cancellations")
r_require_match_count(std_c17 "r_std_async_detach\\(" 1
    "generated standard async-start detach")
r_require_match_count(user_c17 "setjmp|longjmp" 0
    "user async-start checked transfer must use branch-based C17")
r_require_match_count(std_c17 "setjmp|longjmp" 0
    "standard async-start checked transfer must use branch-based C17")
r_require_match_count(user_c17 "r_payload\\.r_ok\\.r_tag" 0
    "user async start/completion carriers must remain flat")
r_require_match_count(std_c17 "r_payload\\.r_ok\\.r_tag" 0
    "standard async start/completion carriers must remain flat")

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
    r_require_match_count(std_c17 "${operation}\\(" 1
        "generated ${operation} call")
    r_require_optional_payload_before_tag(std_c17 "${operation}"
        "generated ${operation} start carrier")
endforeach()

set(move_native_operations
    "r_std_fs_write_file_atomic_no_replace_beneath"
    "r_std_io_read"
    "r_std_io_write"
    "r_std_io_write_all"
    "r_std_io_write_shared"
    "r_std_io_close_input"
    "r_std_io_close_output"
)
foreach(operation IN LISTS move_native_operations)
    r_require_status_before_cleanup(std_c17 "${operation}"
        "generated ${operation} two-phase Move commit")
endforeach()
