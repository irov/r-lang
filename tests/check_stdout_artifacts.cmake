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

function(r_require_present variable pattern description)
    string(REGEX MATCH "${pattern}" match "${${variable}}")
    if(match STREQUAL "")
        message(FATAL_ERROR "${description}: '${pattern}' is absent:\n${${variable}}")
    endif()
endfunction()

function(r_capture_artifact source_file entry_name emit_kind output_variable)
    set(arguments --emit=${emit_kind})
    if(emit_kind STREQUAL "llvm-ir")
        # The emitter's own IR, not the optimizer's.
        list(APPEND arguments --opt-level=0)
    endif()
    if(emit_kind STREQUAL "llvm-ir" OR emit_kind STREQUAL "link-plan")
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

function(r_check_stdout_source source_file entry_name is_async)
    r_capture_artifact("${source_file}" "${entry_name}" hir hir_output)
    r_require_match_count(hir_output "operation=std[.]io::stdout" 1
        "HIR stdout operations")
    r_require_match_count(hir_output
        "[(]move symbol=[^\n]*type=[(]standard \"std[.]io::output\"[)]" 1
        "HIR output moves")

    r_capture_artifact("${source_file}" "${entry_name}" mir mir_output)
    r_require_match_count(mir_output "operation=std[.]io::stdout" 1
        "MIR stdout operations")
    r_require_match_count(mir_output
        "move source=[^\n]*type=[(]standard \"std[.]io::output\"[)]" 1
        "MIR output moves")

    r_capture_artifact("${source_file}" "${entry_name}" llvm-ir ir_output)
    r_require_match_count(ir_output "call [^\n]*@r_std_io_stdout[(]" 1
        "stdout calls")
    # The move and drop glue of std.io::output reach the library's own entries.
    r_require_present(ir_output "call [^\n]*@(r_shim_)?r_std_io_output_move_initialize[(]"
        "output move glue")
    r_require_present(ir_output "call [^\n]*@(r_shim_)?r_std_io_output_destroy[(]"
        "output drop glue")

    if(is_async)
        r_require_match_count(ir_output "call [^\n]*@r_std_io_write_all[(]" 1
            "async stdout writes")
        r_require_match_count(ir_output "call [^\n]*@r_std_array_push[(]" 1
            "stdout payload pushes")
        string(FIND "${ir_output}" "@r_std_io_stdout(" stdout_offset)
        string(FIND "${ir_output}" "@r_std_io_write_all(" write_offset)
        if(stdout_offset EQUAL -1 OR write_offset EQUAL -1 OR
           NOT stdout_offset LESS write_offset)
            message(FATAL_ERROR
                "async stdout acquisition must precede its write:\n${ir_output}")
        endif()
    endif()

    r_capture_artifact("${source_file}" "${entry_name}" link-plan plan_output)
    r_require_match_count(plan_output
        "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
        "std.io link-plan records")
endfunction()

r_check_stdout_source(
    "${SYNC_SOURCE_FILE}"
    "test.codegen.stdout::main"
    FALSE)
r_check_stdout_source(
    "${ASYNC_SOURCE_FILE}"
    "test.codegen.async_stdout::main"
    TRUE)
