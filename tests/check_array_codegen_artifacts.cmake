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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${SYNC_SOURCE_FILE}"
    RESULT_VARIABLE sync_hir_result
    OUTPUT_VARIABLE sync_hir_output
    ERROR_VARIABLE sync_hir_error
)
if(NOT sync_hir_result EQUAL 0)
    message(FATAL_ERROR "sync HIR emit failed (${sync_hir_result}): ${sync_hir_error}")
endif()
r_require_match_count(sync_hir_output "operation=std[.]array::with_capacity" 2
    "sync HIR with_capacity operations")
r_require_match_count(sync_hir_output "operation=std[.]array::push" 2
    "sync HIR push operations")
r_require_match_count(sync_hir_output
    "[(]type [0-9]+ [(]standard \"std[.]array::push_error\" [(]struct \"test[.]codegen[.]array_operations\"::\"pair\"[)][)][)]"
    1 "sync HIR generic push_error(pair) schema")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SYNC_SOURCE_FILE}"
    RESULT_VARIABLE sync_mir_result
    OUTPUT_VARIABLE sync_mir_output
    ERROR_VARIABLE sync_mir_error
)
if(NOT sync_mir_result EQUAL 0)
    message(FATAL_ERROR "sync MIR emit failed (${sync_mir_result}): ${sync_mir_error}")
endif()
r_require_match_count(sync_mir_output "operation=std[.]array::with_capacity" 2
    "sync MIR with_capacity operations")
r_require_match_count(sync_mir_output
    "operation=std[.]array::push arguments=[(]%v[0-9]+ %v[0-9]+[)] call_bounded_borrows=[(]target[)]"
    2 "sync MIR push target call-bounded borrows")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE async_hir_result
    OUTPUT_VARIABLE async_hir_output
    ERROR_VARIABLE async_hir_error
)
if(NOT async_hir_result EQUAL 0)
    message(FATAL_ERROR "async HIR emit failed (${async_hir_result}): ${async_hir_error}")
endif()
r_require_match_count(async_hir_output "operation=std[.]array::with_capacity" 1
    "async HIR with_capacity operation")
r_require_match_count(async_hir_output "operation=std[.]array::push" 1
    "async HIR push operation")
r_require_match_count(async_hir_output
    "[(]type [0-9]+ [(]standard \"std[.]array::push_error\" [(]struct \"test[.]codegen[.]async_array_operations\"::\"pair\"[)][)][)]"
    1 "async HIR generic push_error(pair) schema")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE async_mir_result
    OUTPUT_VARIABLE async_mir_output
    ERROR_VARIABLE async_mir_error
)
if(NOT async_mir_result EQUAL 0)
    message(FATAL_ERROR "async MIR emit failed (${async_mir_result}): ${async_mir_error}")
endif()
r_require_match_count(async_mir_output "operation=std[.]array::with_capacity" 1
    "async MIR with_capacity operation")
r_require_match_count(async_mir_output
    "operation=std[.]array::push arguments=[(]%v[0-9]+ %v[0-9]+[)] call_bounded_borrows=[(]target[)]"
    1 "async MIR push target call-bounded borrow")

# Each with_capacity and push operation is one call of its std.array entry, and the pushed value
# is handed to the entry so that a failed push can return it (Library R-LIB-0019).
foreach(mode IN ITEMS sync async)
    if(mode STREQUAL "sync")
        set(source_file "${SYNC_SOURCE_FILE}")
        set(entry "test.codegen.array_operations::main")
        set(operation_count 2)
    else()
        set(source_file "${ASYNC_SOURCE_FILE}")
        set(entry "test.codegen.async_array_operations::main")
        set(operation_count 1)
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}"
            --emit=llvm-ir
            --entry "${entry}"
            --profile hosted-native-async
            --target-manifest "${TARGET_MANIFEST}"
            "${source_file}"
        RESULT_VARIABLE ir_result
        OUTPUT_VARIABLE ir_output
        ERROR_VARIABLE ir_error
    )
    if(NOT ir_result EQUAL 0)
        message(FATAL_ERROR "${mode} LLVM IR emit failed (${ir_result}): ${ir_error}")
    endif()
    r_require_match_count(ir_output "call [^\n]*@r_std_array_with_capacity[(]" ${operation_count}
        "${mode} with_capacity calls")
    r_require_match_count(ir_output "call [^\n]*@r_std_array_push[(]ptr [^,]+, ptr [^)]+[)]"
        ${operation_count} "${mode} push calls with the staged value")
endforeach()

foreach(mode IN ITEMS sync async)
    if(mode STREQUAL "sync")
        set(source_file "${SYNC_SOURCE_FILE}")
        set(entry "test.codegen.array_operations::main")
    else()
        set(source_file "${ASYNC_SOURCE_FILE}")
        set(entry "test.codegen.async_array_operations::main")
    endif()
    execute_process(
        COMMAND "${R_FRONT_EXECUTABLE}"
            --emit=link-plan
            --entry "${entry}"
            --profile hosted-native-async
            --target-manifest "${TARGET_MANIFEST}"
            "${source_file}"
        RESULT_VARIABLE plan_result
        OUTPUT_VARIABLE plan_output
        ERROR_VARIABLE plan_error
    )
    if(NOT plan_result EQUAL 0)
        message(FATAL_ERROR "${mode} link-plan emit failed (${plan_result}): ${plan_error}")
    endif()
    r_require_match_count(plan_output
        "[(]library module=\"std[.]array\" target=\"r_std_array\"[)]" 1
        "${mode} exactly one std.array link-plan record")
endforeach()
