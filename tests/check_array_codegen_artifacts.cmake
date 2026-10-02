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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.array_operations::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SYNC_SOURCE_FILE}"
    RESULT_VARIABLE sync_c17_result
    OUTPUT_VARIABLE sync_c17_output
    ERROR_VARIABLE sync_c17_error
)
if(NOT sync_c17_result EQUAL 0)
    message(FATAL_ERROR "sync C17 emit failed (${sync_c17_result}): ${sync_c17_error}")
endif()
r_require_match_count(sync_c17_output "#include \"r_std_array[.]h\"" 1
    "sync generated std.array include")
r_require_match_count(sync_c17_output "r_std_array_with_capacity[(]" 2
    "sync generated with_capacity calls")
r_require_match_count(sync_c17_output "r_ap_[0-9]+_[0-9]+[(]" 3
    "sync generated push calls")
r_require_match_count(sync_c17_output "RStdAllocError r_reason" 1
    "sync generic push_error reason field")
r_require_match_count(sync_c17_output "r_a[0-9]+ r_value" 1
    "sync generic push_error pair value field")
r_require_match_count(sync_c17_output
    "r_payload[.]r_allocation_failed[.]r_value =" 2
    "sync failure returns staged pair values")
r_require_match_count(sync_c17_output "r_type_drop_d00000006" 0
    "sync move-only result glue omits unused drop helper")
r_require_match_count(sync_c17_output "R_INTERNAL_ASSERT[(]" 0
    "sync generated C omits assertion infrastructure")
r_require_match_count(sync_c17_output "R_RUNTIME_PANIC_CONTRACT_VIOLATION" 4
    "sync fallthrough and main carrier integrity traps remain")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_array_operations::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE async_c17_result
    OUTPUT_VARIABLE async_c17_output
    ERROR_VARIABLE async_c17_error
)
if(NOT async_c17_result EQUAL 0)
    message(FATAL_ERROR "async C17 emit failed (${async_c17_result}): ${async_c17_error}")
endif()
r_require_match_count(async_c17_output "#include \"r_std_array[.]h\"" 1
    "async generated std.array include")
r_require_match_count(async_c17_output "r_std_array_with_capacity[(]" 1
    "async generated with_capacity call")
r_require_match_count(async_c17_output "r_ap_[0-9]+_[0-9]+[(]" 2
    "async generated push call")
r_require_match_count(async_c17_output "RStdAllocError r_reason" 1
    "async generic push_error reason field")
r_require_match_count(async_c17_output "r_a[0-9]+ r_value" 1
    "async generic push_error pair value field")
r_require_match_count(async_c17_output
    "r_payload[.]r_allocation_failed[.]r_value =" 1
    "async failure returns staged pair value")
r_require_match_count(async_c17_output "R_INTERNAL_ASSERT[(]" 0
    "async generated C omits assertion infrastructure")
r_require_match_count(async_c17_output "R_RUNTIME_PANIC_CONTRACT_VIOLATION" 7
    "async state, await and root traps remain")

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
