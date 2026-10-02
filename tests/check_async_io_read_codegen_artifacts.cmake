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

function(r_capture_artifact emit_kind output_variable)
    set(arguments --emit=${emit_kind})
    if(emit_kind STREQUAL "c17" OR emit_kind STREQUAL "link-plan")
        list(APPEND arguments
            --entry test.codegen.async_io_read::main
            --profile hosted-native-async
            --target-manifest "${TARGET_MANIFEST}")
    endif()
    list(APPEND arguments "${SOURCE_FILE}")
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

r_capture_artifact(hir hir_output)
r_capture_artifact(hir hir_repeated)
r_require_deterministic(hir_output hir_repeated "HIR std.io::read artifact")
r_require_match_count(hir_output "operation=std[.]io::stdin" 1
    "HIR stdin operations")
r_require_match_count(hir_output "operation=std[.]io::read" 1
    "HIR read operations")
r_require_match_count(hir_output
    "[(]await [^\n]*type=[(]standard \"std[.]io::read_result\"[)]" 1
    "HIR read awaits")
r_require_match_count(hir_output
    "[(]drop symbol=[^\n]*name=\"completed\"" 1
    "HIR whole read-result drops")

r_capture_artifact(mir mir_output)
r_capture_artifact(mir mir_repeated)
r_require_deterministic(mir_output mir_repeated "MIR std.io::read artifact")
r_require_match_count(mir_output
    "standard_call operation=std[.]io::read stream=%v[0-9]+ buffer=%v[0-9]+ deadline=%v[0-9]+ call_bounded_borrows=[(]stream[)] staged_moves=[(]buffer[)]" 1
    "MIR ownership-aware read operations")
r_require_match_count(mir_output
    "await [^\n]*type=[(]standard \"std[.]io::read_result\"[)]" 1
    "MIR read awaits")

r_capture_artifact(c17 c17_output)
r_capture_artifact(c17 c17_repeated)
r_require_deterministic(c17_output c17_repeated "C17 std.io::read artifact")
r_require_match_count(c17_output "#include \"r_std_io[.]h\"" 1
    "generated std.io includes")
r_require_match_count(c17_output "r_std_io_stdin[(]" 1
    "generated stdin calls")
r_require_match_count(c17_output "r_std_io_read[(]" 1
    "generated read calls")
r_require_match_count(c17_output "RStdIoDeadline r_io_deadline_[0-9]+" 1
    "generated deadline adapters")
r_require_match_count(c17_output "RRuntimeArray r_io_buffer_before_[0-9]+" 0
    "generated C omits debug ownership snapshots")
r_require_match_count(c17_output
    "if [(]r_io_start_[0-9]+[.]is_ok[)] [{][\n ]+frame->r_l[0-9]+_initialized = 0" 1
    "generated successful-start ownership transition")
r_require_match_count(c17_output "RStdIoTaskStartResult r_io_start_[0-9]+" 1
    "generated start-result adapters")
r_require_match_count(c17_output "r_io_start_[0-9]+[.]task = NULL" 1
    "generated task ownership transfers")
r_require_match_count(c17_output "r_std_io_read_result_destroy[(]" 1
    "generated whole read-result drops")

r_capture_artifact(link-plan plan_output)
r_capture_artifact(link-plan plan_repeated)
r_require_deterministic(plan_output plan_repeated "std.io::read link-plan artifact")
r_require_match_count(plan_output
    "[(]library module=\"std[.]array\" target=\"r_std_array\"[)]" 1
    "std.array link-plan records")
r_require_match_count(plan_output
    "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
    "std.io link-plan records")
