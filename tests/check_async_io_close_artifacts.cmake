if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED INPUT_SOURCE_FILE OR
   NOT DEFINED OUTPUT_SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, INPUT_SOURCE_FILE, OUTPUT_SOURCE_FILE and TARGET_MANIFEST are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

function(r_capture_artifact emit_kind source_file entry output_variable)
    set(arguments --emit=${emit_kind})
    if(emit_kind STREQUAL "llvm-ir")
        # The emitter's own IR, not the optimizer's.
        list(APPEND arguments --opt-level=0)
    endif()
    if(emit_kind STREQUAL "llvm-ir" OR emit_kind STREQUAL "link-plan")
        list(APPEND arguments
            --entry "${entry}"
            --profile hosted-native-async
            --target-manifest "${TARGET_MANIFEST}")
    endif()
    list(APPEND arguments "${source_file}")
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

function(r_require_text variable expected description)
    string(FIND "${${variable}}" "${expected}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${description}: exact text not found:\n${${variable}}")
    endif()
endfunction()

function(r_check_close label source_file entry operation handle_type native_call)
    r_capture_artifact(hir "${source_file}" "${entry}" hir_output)
    r_capture_artifact(hir "${source_file}" "${entry}" hir_repeated)
    r_require_deterministic(hir_output hir_repeated "${label} HIR artifact")
    r_require_match_count(hir_output "operation=std[.]io::${operation}" 1
        "${label} HIR operations")
    r_require_text(hir_output
        "standard_call operation=std.io::${operation} type=(task void (effects (standard \"std.io::io_error\"))) task=(carrier (task void (effects (standard \"std.io::io_error\"))) (effects (standard \"std.async::start_error\")))"
        "${label} exact start carrier types")
    r_require_match_count(hir_output
        "[(]await [^\n]*type=void task=[(]task void [(]effects [(]standard \"std[.]io::io_error\"[)][)][)]" 1
        "${label} checked awaits")
    r_require_match_count(hir_output
        "[(]move symbol=[0-9]+ name=\"stream\" type=[(]standard \"std[.]io::${handle_type}\"[)]" 2
        "${label} staged and rollback-visible handle moves")
    r_require_match_count(hir_output
        "local symbol=[0-9]+ name=\"retained\" type=[(]standard \"std[.]io::${handle_type}\"[)]" 1
        "${label} start-error rollback bindings")

    r_capture_artifact(mir "${source_file}" "${entry}" mir_output)
    r_capture_artifact(mir "${source_file}" "${entry}" mir_repeated)
    r_require_deterministic(mir_output mir_repeated "${label} MIR artifact")
    r_require_match_count(mir_output
        "standard_call operation=std[.]io::${operation}[^\n]*staged_moves=[(]stream[)]" 1
        "${label} ownership-aware MIR operations")
    r_require_match_count(mir_output
        "move source=%local[0-9]+ type=[(]standard \"std[.]io::${handle_type}\"[)] async_staged=true" 1
        "${label} direct staged handle moves")
    r_require_match_count(mir_output
        "local place=%local[0-9]+ name=\"retained\" type=[(]standard \"std[.]io::${handle_type}\"[)]" 1
        "${label} MIR start-error rollback bindings")
    r_require_text(mir_output
        "type=(carrier (task void (effects (standard \"std.io::io_error\"))) (effects (standard \"std.async::start_error\"))) task=(task void (effects (standard \"std.io::io_error\")))"
        "${label} exact MIR start carrier types")
    r_require_match_count(mir_output
        "await [^\n]*task=[(]task void [(]effects [(]standard \"std[.]io::io_error\"[)][)][)] type=[(]carrier void [(]effects [(]standard \"std[.]io::io_error\"[)][)][)]" 1
        "${label} checked MIR awaits")

    r_capture_artifact(llvm-ir "${source_file}" "${entry}" ir_output)
    r_capture_artifact(llvm-ir "${source_file}" "${entry}" ir_repeated)
    r_require_deterministic(ir_output ir_repeated "${label} LLVM IR artifact")
    r_require_match_count(ir_output "call [^\n]*@${native_call}[(]" 1
        "${label} library close calls")
    # R-STMT-0019 (L23): the close start narrows its deadline argument by the structural
    # deadline.
    r_require_match_count(ir_output "call [^\n]*@r_runtime_task_deadline_narrow[(]" 1
        "${label} deadline narrowing")

    r_capture_artifact(link-plan "${source_file}" "${entry}" plan_output)
    r_capture_artifact(link-plan "${source_file}" "${entry}" plan_repeated)
    r_require_deterministic(plan_output plan_repeated "${label} link-plan artifact")
    r_require_match_count(plan_output
        "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
        "${label} std.io link-plan records")
endfunction()

r_check_close(
    "close_input"
    "${INPUT_SOURCE_FILE}"
    "test.codegen.async_io_close_input::main"
    "close_input"
    "input"
    "r_std_io_close_input")
r_check_close(
    "close_output"
    "${OUTPUT_SOURCE_FILE}"
    "test.codegen.async_io_close_output::main"
    "close_output"
    "output"
    "r_std_io_close_output")
