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
            --entry test.codegen.async_io_write_result::main
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

function(r_require_text variable expected description)
    string(FIND "${${variable}}" "${expected}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${description}: exact text not found:\n${${variable}}")
    endif()
endfunction()

r_capture_artifact(hir hir_output)
r_capture_artifact(hir hir_repeated)
r_require_deterministic(hir_output hir_repeated "HIR std.io::write_result artifact")
r_require_match_count(hir_output "operation=std[.]io::write" 1
    "HIR write operations")
r_require_match_count(hir_output
    "[(]await [^\n]*type=[(]standard \"std[.]io::write_result\"[)]" 1
    "HIR write-result awaits")
r_require_match_count(hir_output
    "case pattern=variant value=0[^\n]*type=[(]standard_payload \"std[.]io::write_result::written\"[)] binding=move" 1
    "HIR written payload variants")
r_require_match_count(hir_output
    "case pattern=variant value=1[^\n]*type=[(]standard_payload \"std[.]io::write_result::failed\"[)] binding=move" 1
    "HIR failed payload variants")
r_require_match_count(hir_output "[(]drop symbol=[^\n]*name=\"payload\"" 1
    "HIR written payload drops")
r_require_match_count(hir_output "[(]drop symbol=[^\n]*name=\"failure\"" 1
    "HIR failed payload drops")

r_capture_artifact(mir mir_output)
r_capture_artifact(mir mir_repeated)
r_require_deterministic(mir_output mir_repeated "MIR std.io::write_result artifact")
r_require_match_count(mir_output
    "standard_call operation=std[.]io::write stream=%v[0-9]+ buffer=%v[0-9]+ deadline=%v[0-9]+ call_bounded_borrows=[(]stream[)] staged_moves=[(]buffer[)]" 1
    "MIR ownership-aware write operations")
r_require_match_count(mir_output
    "variant_payload value=%v[0-9]+ tag=0 type=[(]standard_payload \"std[.]io::write_result::written\"[)]" 1
    "MIR written payload variants")
r_require_match_count(mir_output
    "variant_payload value=%v[0-9]+ tag=1 type=[(]standard_payload \"std[.]io::write_result::failed\"[)]" 1
    "MIR failed payload variants")

r_capture_artifact(c17 c17_output)
r_capture_artifact(c17 c17_repeated)
r_require_deterministic(c17_output c17_repeated "C17 std.io::write_result artifact")
r_require_match_count(c17_output "#include \"r_std_io[.]h\"" 1
    "generated std.io includes")
r_require_match_count(c17_output "r_std_io_write[(]" 1
    "generated write calls")
r_require_match_count(c17_output "R_STD_IO_WRITE_RESULT_WRITTEN" 0
    "generated C omits assertion-only written-tag references")
r_require_match_count(c17_output "R_STD_IO_WRITE_RESULT_FAILED" 0
    "generated C omits assertion-only failed-tag references")
r_require_match_count(c17_output
    "frame->r_v00000027 = [(]uint32_t[)]frame->r_v00000026[.]kind" 1
    "generated direct write-result discrimination")
r_require_match_count(c17_output "R_INTERNAL_ASSERT[(]" 0
    "generated C omits assertions")
r_require_match_count(c17_output "r_std_io_write_result_move_initialize[(]" 1
    "generated write-result moves")

string(REPLACE ";" "<SEMICOLON>" c17_without_semicolons "${c17_output}")
r_require_match_count(c17_without_semicolons
    "struct r_a[0-9]+ [{]\n    RRuntimeArray r_m00000001<SEMICOLON>\n    size_t r_m00000002<SEMICOLON>\n[}]<SEMICOLON>" 1
    "generated written payload layouts")
r_require_match_count(c17_without_semicolons
    "struct r_a[0-9]+ [{]\n    RStdIoError r_m00000001<SEMICOLON>\n    size_t r_m00000002<SEMICOLON>\n    RRuntimeArray r_m00000003<SEMICOLON>\n[}]<SEMICOLON>" 1
    "generated failed payload layouts")
r_require_match_count(c17_output "= [(]RStdIoWriteResult[)][{]0[}]" 2
    "generated native-owner clears")
r_require_match_count(c17_output
    "frame->r_v[0-9]+[.]r_m00000002 = frame->r_v[0-9]+[.]count" 2
    "generated written-count transfers")
r_require_match_count(c17_output
    "frame->r_v[0-9]+[.]r_m00000001 = frame->r_v[0-9]+[.]error" 1
    "generated failed-error transfers")
r_require_match_count(c17_output
    "r_type_move_array_gate[(]&frame->r_v[0-9]+[.]r_m00000001, &frame->r_v[0-9]+[.]buffer[)]" 1
    "generated written-buffer transfers")
r_require_match_count(c17_output
    "r_type_move_array_gate[(]&frame->r_v[0-9]+[.]r_m00000003, &frame->r_v[0-9]+[.]buffer[)]" 1
    "generated failed-buffer transfers")
r_require_text(c17_output [=[frame->r_v00000030.r_m00000002 = frame->r_v00000026.count;
            r_type_move_array_gate(&frame->r_v00000030.r_m00000001, &frame->r_v00000026.buffer);
            frame->r_v00000026 = (RStdIoWriteResult){0};]=]
    "generated ordered written-payload transfer")
r_require_text(c17_output [=[frame->r_v00000044.r_m00000001 = frame->r_v00000026.error;
            frame->r_v00000044.r_m00000002 = frame->r_v00000026.count;
            r_type_move_array_gate(&frame->r_v00000044.r_m00000003, &frame->r_v00000026.buffer);
            frame->r_v00000026 = (RStdIoWriteResult){0};]=]
    "generated ordered failed-payload transfer")

r_capture_artifact(link-plan plan_output)
r_capture_artifact(link-plan plan_repeated)
r_require_deterministic(plan_output plan_repeated "std.io::write_result link-plan artifact")
r_require_match_count(plan_output
    "[(]library module=\"std[.]array\" target=\"r_std_array\"[)]" 1
    "std.array link-plan records")
r_require_match_count(plan_output
    "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
    "std.io link-plan records")
