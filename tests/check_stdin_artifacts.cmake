if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SYNC_SOURCE_FILE OR
   NOT DEFINED ASYNC_SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SYNC_SOURCE_FILE, ASYNC_SOURCE_FILE and TARGET_MANIFEST are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

function(r_capture_artifact source_file entry_name emit_kind output_variable)
    set(arguments --emit=${emit_kind})
    if(emit_kind STREQUAL "c17" OR emit_kind STREQUAL "link-plan")
        list(APPEND arguments
            --entry "${entry_name}"
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
        message(FATAL_ERROR
            "${emit_kind} emit failed for ${source_file} (${result}): ${error}")
    endif()
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

function(r_require_deterministic first second description)
    if(NOT "${${first}}" STREQUAL "${${second}}")
        message(FATAL_ERROR "${description} is not deterministic")
    endif()
endfunction()

function(r_check_stdin_source source_file entry_name)
    r_capture_artifact("${source_file}" "${entry_name}" hir hir_output)
    r_capture_artifact("${source_file}" "${entry_name}" hir hir_repeated)
    r_require_deterministic(hir_output hir_repeated "HIR stdin artifact")
    r_require_match_count(hir_output "operation=std[.]io::stdin" 1
        "HIR stdin operations")
    r_require_match_count(hir_output
        "[(]move symbol=[^\n]*type=[(]standard \"std[.]io::input\"[)]" 1
        "HIR input moves")
    r_require_match_count(hir_output
        "[(]drop symbol=[^\n]*name=\"selected\"" 1
        "HIR input drops")

    r_capture_artifact("${source_file}" "${entry_name}" mir mir_output)
    r_capture_artifact("${source_file}" "${entry_name}" mir mir_repeated)
    r_require_deterministic(mir_output mir_repeated "MIR stdin artifact")
    r_require_match_count(mir_output "operation=std[.]io::stdin" 1
        "MIR stdin operations")
    r_require_match_count(mir_output
        "move source=[^\n]*type=[(]standard \"std[.]io::input\"[)]" 1
        "MIR input moves")
    r_require_match_count(mir_output
        "[(]drop place=" 1
        "MIR input drops")

    r_capture_artifact("${source_file}" "${entry_name}" c17 c17_output)
    r_capture_artifact("${source_file}" "${entry_name}" c17 c17_repeated)
    r_require_deterministic(c17_output c17_repeated "C17 stdin artifact")
    r_require_match_count(c17_output "#include \"r_std_io[.]h\"" 1
        "generated std.io includes")
    r_require_match_count(c17_output "r_std_io_stdin[(]" 1
        "generated stdin calls")
    r_require_match_count(c17_output "r_std_io_input_move_initialize[(]" 1
        "generated input moves")
    r_require_match_count(c17_output "r_std_io_input_destroy[(]" 1
        "generated input drops")

    r_capture_artifact("${source_file}" "${entry_name}" link-plan plan_output)
    r_capture_artifact("${source_file}" "${entry_name}" link-plan plan_repeated)
    r_require_deterministic(plan_output plan_repeated "stdin link-plan artifact")
    r_require_match_count(plan_output
        "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
        "std.io link-plan records")
endfunction()

r_check_stdin_source(
    "${SYNC_SOURCE_FILE}"
    "test.codegen.stdin::main")
r_check_stdin_source(
    "${ASYNC_SOURCE_FILE}"
    "test.codegen.async_stdin::main")
