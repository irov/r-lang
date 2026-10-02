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

function(r_require_text variable expected description)
    string(FIND "${${variable}}" "${expected}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${description}: exact text not found:\n${${variable}}")
    endif()
endfunction()

function(r_capture_artifact emit_kind output_variable)
    set(arguments --emit=${emit_kind})
    if(emit_kind STREQUAL "c17" OR emit_kind STREQUAL "link-plan")
        list(APPEND arguments
            --entry test.codegen.async_io_write_shared::main
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
r_require_deterministic(hir_output hir_repeated "HIR std.io::write_shared artifact")
r_require_match_count(hir_output "operation=std[.]io::write_shared" 1
    "HIR write_shared operations")
r_require_text(hir_output
    "standard_call operation=std.io::write_shared type=(task (standard \"std.io::shared_write_result\")) task=(carrier (task (standard \"std.io::shared_write_result\")) (effects (standard \"std.async::start_error\")))"
    "HIR exact write_shared start carrier types")
r_require_match_count(hir_output
    "type=[(]arc [(]array u8[)][)] payload=[(]array u8[)] owner=arc" 1
    "HIR shared byte-array allocations")
r_require_match_count(hir_output
    "[(]await [^\n]*type=[(]standard \"std[.]io::shared_write_result\"[)]" 1
    "HIR shared-write-result awaits")
r_require_match_count(hir_output
    "case pattern=variant value=0[^\n]*type=[(]arc [(]array u8[)][)] binding=move" 1
    "HIR written owner variants")
r_require_match_count(hir_output
    "case pattern=variant value=1[^\n]*type=[(]standard_payload \"std[.]io::shared_write_result::failed\"[)] binding=move" 1
    "HIR failed shared-write variants")
r_require_match_count(hir_output "[(]drop symbol=[^\n]*name=\"returned\"" 1
    "HIR written owner drops")
r_require_match_count(hir_output "[(]drop symbol=[^\n]*name=\"failure\"" 1
    "HIR failed payload drops")
r_require_match_count(hir_output "[(]drop symbol=[^\n]*name=\"shared\"" 1
    "HIR start-error owner rollback drops")

r_capture_artifact(mir mir_output)
r_capture_artifact(mir mir_repeated)
r_require_deterministic(mir_output mir_repeated "MIR std.io::write_shared artifact")
r_require_match_count(mir_output
    "standard_call operation=std[.]io::write_shared stream=%v[0-9]+ buffer=%v[0-9]+ offset=%v[0-9]+ length=%v[0-9]+ deadline=%v[0-9]+ call_bounded_borrows=[(]stream[)] staged_moves=[(]buffer[)]" 1
    "MIR ownership-aware write_shared operations")
r_require_text(mir_output
    "type=(carrier (task (standard \"std.io::shared_write_result\")) (effects (standard \"std.async::start_error\"))) task=(task (standard \"std.io::shared_write_result\"))"
    "MIR exact write_shared start carrier types")
r_require_match_count(mir_output
    "move source=%local[0-9]+ type=[(]arc [(]array u8[)][)] async_staged=true" 1
    "MIR direct staged shared-owner moves")
r_require_match_count(mir_output
    "variant_payload value=%v[0-9]+ tag=0 type=[(]arc [(]array u8[)][)]" 1
    "MIR written owner variants")
r_require_match_count(mir_output
    "variant_payload value=%v[0-9]+ tag=1 type=[(]standard_payload \"std[.]io::shared_write_result::failed\"[)]" 1
    "MIR failed shared-write variants")

r_capture_artifact(c17 c17_output)
r_capture_artifact(c17 c17_repeated)
r_require_deterministic(c17_output c17_repeated "C17 std.io::write_shared artifact")
r_require_match_count(c17_output "#include \"r_std_io[.]h\"" 1
    "generated std.io includes")
r_require_match_count(c17_output "#include \"r_runtime_arc[.]h\"" 1
    "generated runtime arc includes")
r_require_match_count(c17_output "r_std_io_write_shared[(]" 1
    "generated write_shared calls")
r_require_match_count(c17_output "R_STD_IO_SHARED_WRITE_RESULT_WRITTEN" 0
    "generated C omits assertion-only written-tag references")
r_require_match_count(c17_output "R_STD_IO_SHARED_WRITE_RESULT_FAILED" 0
    "generated C omits assertion-only failed-tag references")
r_require_match_count(c17_output
    "frame->r_v00000031 = [(]uint32_t[)]frame->r_v00000030[.]kind" 1
    "generated direct shared-write outcome discrimination")
r_require_match_count(c17_output "r_std_io_shared_write_result_move_initialize[(]" 1
    "generated shared-write-result moves")
r_require_match_count(c17_output "const RRuntimeArc r_io_shared_buffer_before_[0-9]+" 0
    "generated C omits debug ownership snapshots")
r_require_match_count(c17_output
    "R_INTERNAL_ASSERT[(]" 0
    "generated C omits assertions")
r_require_match_count(c17_output
    "if [(]r_io_start_[0-9]+[.]is_ok[)] [{][\n ]+frame->r_l[0-9]+_initialized = 0" 1
    "generated direct successful-start ownership transition")
r_require_match_count(c17_output
    "if [(]frame->r_l[0-9]+[.]control != NULL[)]" 0
    "generated successful-start runtime guards")
r_require_match_count(c17_output "r_io_start_[0-9]+[.]task = NULL" 1
    "generated task ownership transfers")
r_require_match_count(c17_output
    "r_type_move_arc_gate[(]&frame->[^,\n]+, &frame->[^)\n]+[.]buffer[)]" 2
    "generated native shared-owner transfers")
r_require_match_count(c17_output "= [(]RStdIoSharedWriteResult[)][{]0[}]" 2
    "generated native-owner clears")

string(REPLACE ";" "<SEMICOLON>" c17_without_semicolons "${c17_output}")
r_require_match_count(c17_without_semicolons
    "struct r_a[0-9]+ [{]\n    RStdIoError r_m00000001<SEMICOLON>\n    size_t r_m00000002<SEMICOLON>\n    RRuntimeArc r_m00000003<SEMICOLON>\n[}]<SEMICOLON>" 1
    "generated failed shared-write payload layouts")
r_require_match_count(c17_output
    "[.]r_m00000001 = [^;\n]+[.]error" 1
    "generated failed-error transfers")
r_require_match_count(c17_output
    "[.]r_m00000002 = [^;\n]+[.]written" 1
    "generated failed-written transfers")
r_require_match_count(c17_output
    "r_type_move_arc_gate[(]&[^,\n]+[.]r_m00000003, &[^)\n]+[.]buffer[)]" 1
    "generated failed-owner transfers")

r_capture_artifact(link-plan plan_output)
r_capture_artifact(link-plan plan_repeated)
r_require_deterministic(plan_output plan_repeated "std.io::write_shared link-plan artifact")
r_require_match_count(plan_output "[(]library module=" 3
    "write_shared link-plan library records")
r_require_match_count(plan_output
    "[(]library module=\"std[.]array\" target=\"r_std_array\"[)]" 1
    "std.array link-plan records")
r_require_match_count(plan_output
    "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
    "std.io link-plan records")
