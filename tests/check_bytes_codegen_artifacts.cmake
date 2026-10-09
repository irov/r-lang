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

set(r_bytes_operations
    append
    append_u8
    append_u16_le
    append_u32_le
    append_u64_le
    with_capacity
)
set(r_hash_operations crc32 md5 sha1 sha256 sha512)
set(r_utf8_operations is_valid validate)
set(r_bits_operations read align_byte)

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${SOURCE_FILE}"
    RESULT_VARIABLE hir_result
    OUTPUT_VARIABLE hir_output
    ERROR_VARIABLE hir_error
)
if(NOT hir_result EQUAL 0)
    message(FATAL_ERROR "bytes HIR emit failed (${hir_result}): ${hir_error}")
endif()
foreach(operation IN LISTS r_bytes_operations)
    r_require_match_count(hir_output "operation=std[.]bytes::${operation} type=" 2
        "bytes HIR ${operation} operations")
endforeach()
foreach(operation IN LISTS r_hash_operations)
    r_require_match_count(hir_output "operation=std[.]hash::${operation} type=" 2
        "hash HIR ${operation} operations")
endforeach()
r_require_match_count(hir_output "operation=std[.]utf8::is_valid type=" 8
    "UTF-8 HIR direct byte-view sources")
r_require_match_count(hir_output "operation=std[.]utf8::validate type=" 9
    "UTF-8 HIR validate direct byte-view sources")
r_require_match_count(hir_output "operation=std[.]bits::read type=" 4
    "bits HIR read operations")
r_require_match_count(hir_output "operation=std[.]bits::align_byte type=" 2
    "bits HIR align operations")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "bytes MIR emit failed (${mir_result}): ${mir_error}")
endif()
foreach(operation IN LISTS r_bytes_operations)
    r_require_match_count(mir_output "operation=std[.]bytes::${operation} arguments=" 2
        "bytes MIR ${operation} operations")
endforeach()
foreach(operation IN LISTS r_hash_operations)
    r_require_match_count(mir_output "operation=std[.]hash::${operation} arguments=" 2
        "hash MIR ${operation} operations")
endforeach()
r_require_match_count(mir_output "operation=std[.]utf8::is_valid arguments=" 8
    "UTF-8 MIR direct byte-view sources")
r_require_match_count(mir_output "operation=std[.]utf8::validate arguments=" 9
    "UTF-8 MIR validate direct byte-view sources")
r_require_match_count(mir_output "operation=std[.]bits::read arguments=" 4
    "bits MIR read operations")
r_require_match_count(mir_output "operation=std[.]bits::align_byte arguments=" 2
    "bits MIR align operations")
r_require_match_count(mir_output
    "operation=std[.]bytes::append arguments=[(]%v[0-9]+ %v[0-9]+[)] call_bounded_borrows=[(]target source[)]"
    2 "bytes MIR append target/source call-bounded borrows")
foreach(operation IN ITEMS append_u8 append_u16_le append_u32_le append_u64_le)
    r_require_match_count(mir_output
        "operation=std[.]bytes::${operation} arguments=[(]%v[0-9]+ %v[0-9]+[)] call_bounded_borrows=[(]target[)]"
        2 "bytes MIR ${operation} target call-bounded borrows")
endforeach()
foreach(operation IN ITEMS crc32 md5 sha1 sha256 sha512)
    r_require_match_count(mir_output
        "operation=std[.]hash::${operation} arguments=[(]%v[0-9]+[)] call_bounded_borrows=[(]source[)]"
        2 "hash MIR ${operation} source call-bounded borrow")
endforeach()
r_require_match_count(mir_output
    "operation=std[.]utf8::is_valid arguments=[(]%v[0-9]+[)] call_bounded_borrows=[(]source[)]"
    8 "UTF-8 MIR source call-bounded borrows")
r_require_match_count(mir_output
    "operation=std[.]utf8::validate arguments=[(]%v[0-9]+[)] call_bounded_borrows=[(]source[)]"
    9 "UTF-8 validate MIR source call-bounded borrows")
r_require_match_count(mir_output
    "operation=std[.]bits::read arguments=[(]%v[0-9]+ %v[0-9]+ %v[0-9]+[)] call_bounded_borrows=[(]input reader[)]"
    4 "bits read MIR byte-view and reader call-bounded borrows")
r_require_match_count(mir_output
    "operation=std[.]bits::align_byte arguments=[(]%v[0-9]+[)] call_bounded_borrows=[(]reader[)]"
    2 "bits align MIR reader call-bounded borrows")

set(r_ir_arguments
    --emit=llvm-ir --opt-level=0
    --entry test.codegen.bytes_operations::main
    --profile hosted-native-async
    --target-manifest "${TARGET_MANIFEST}"
    "${SOURCE_FILE}"
)
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${r_ir_arguments}
    RESULT_VARIABLE ir_result
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR "bytes LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${r_ir_arguments}
    RESULT_VARIABLE repeated_ir_result
    OUTPUT_VARIABLE repeated_ir_output
    ERROR_VARIABLE repeated_ir_error
)
if(NOT repeated_ir_result EQUAL 0)
    message(FATAL_ERROR
        "repeated bytes LLVM IR emit failed (${repeated_ir_result}): ${repeated_ir_error}")
endif()
if(NOT ir_output STREQUAL repeated_ir_output)
    message(FATAL_ERROR "bytes LLVM IR emission is not deterministic")
endif()
# Every operation of the HIR above is one call of its library entry.
foreach(operation IN LISTS r_bytes_operations)
    r_require_match_count(ir_output "call [^\n]*@r_std_bytes_${operation}[(]" 2
        "${operation} calls")
endforeach()
foreach(operation IN LISTS r_hash_operations)
    r_require_match_count(ir_output "call [^\n]*@r_std_hash_${operation}[(]" 2
        "${operation} calls")
endforeach()
r_require_match_count(ir_output "call [^\n]*@r_std_utf8_is_valid[(]" 8
    "UTF-8 validation calls")
r_require_match_count(ir_output "call [^\n]*@r_std_utf8_validate[(]" 9
    "UTF-8 view validation calls")
r_require_match_count(ir_output "call [^\n]*@r_std_bits_read[(]" 4
    "bits read calls")
r_require_match_count(ir_output "call [^\n]*@r_std_bits_align_byte[(]" 2
    "bits align calls")
# The three empty byte arrays ({} of bytes and array<u8>) are initialized by the runtime.
r_require_match_count(ir_output "call [^\n]*@r_runtime_array_initialize[(]" 3
    "empty bytes runtime initialization")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen.bytes_operations::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_output
    ERROR_VARIABLE plan_error
)
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "bytes link-plan emit failed (${plan_result}): ${plan_error}")
endif()
r_require_match_count(plan_output
    "[(]library module=\"std[.]bytes\" target=\"r_std_bytes\"[)]" 1
    "exactly one std.bytes link-plan record")
r_require_match_count(plan_output
    "[(]library module=\"std[.]hash\" target=\"r_std_hash\"[)]" 1
    "exactly one std.hash link-plan record")
r_require_match_count(plan_output
    "[(]library module=\"std[.]utf8\" target=\"r_std_utf8\"[)]" 1
    "exactly one std.utf8 link-plan record")
r_require_match_count(plan_output
    "[(]library module=\"std[.]bits\" target=\"r_std_bits\"[)]" 1
    "exactly one std.bits link-plan record")
string(FIND "${plan_output}" "module=\"std.bytes\"" bytes_offset)
string(FIND "${plan_output}" "module=\"std.hash\"" hash_offset)
string(FIND "${plan_output}" "module=\"std.utf8\"" utf8_offset)
string(FIND "${plan_output}" "module=\"std.bits\"" bits_offset)
if(bytes_offset LESS 0 OR hash_offset LESS bytes_offset OR utf8_offset LESS hash_offset OR bits_offset LESS utf8_offset)
    message(FATAL_ERROR "byte modules are not in normative bytes/hash/utf8/bits order:\n${plan_output}")
endif()
