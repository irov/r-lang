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
    message(FATAL_ERROR "sync checked-retry HIR emit failed (${sync_hir_result}): ${sync_hir_error}")
endif()
r_require_match_count(sync_hir_output
    "operation=std[.]sync::call_once type=void initializer="
    6 "call_once HIR operations")
r_require_match_count(sync_hir_output
    "operation=std[.]sync::call_once_force type=void initializer="
    3 "checked call_once_force HIR operations")
r_require_match_count(sync_hir_output
    "operation=std[.]sync::get_or_init type=[(]const_borrow"
    7 "get_or_init HIR operations")
r_require_match_count(sync_hir_output
    "sync_contract=[(]carrier [(]const_borrow [(]struct \"test[.]codegen[.]sync_checked_retry\"::\"move_value\"[)][)]"
    3 "public Move-value borrow carriers")
r_require_match_count(sync_hir_output
    "callback_storage=[(]carrier [(]struct \"test[.]codegen[.]sync_checked_retry\"::\"move_value\"[)]"
    3 "hidden Move-value callback carriers")
r_require_match_count(sync_hir_output
    "callback_storage=[(]carrier void [(]effects [(]struct \"test[.]codegen[.]sync_checked_retry\"::\"alternate_error\"[)] [(]struct \"test[.]codegen[.]sync_checked_retry\"::\"retry_error\"[)][)][)]"
    2 "canonical multiple-error callback carriers")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE async_mir_result
    OUTPUT_VARIABLE async_mir_output
    ERROR_VARIABLE async_mir_error
)
if(NOT async_mir_result EQUAL 0)
    message(FATAL_ERROR
        "async checked-retry MIR emit failed (${async_mir_result}): ${async_mir_error}")
endif()
r_require_match_count(async_mir_output
    "operation=std[.]sync::call_once arguments=[(]%v[0-9]+[)]"
    2 "async MIR call_once operations")
r_require_match_count(async_mir_output
    "operation=std[.]sync::get_or_init arguments=[(]%v[0-9]+[)]"
    3 "async MIR get_or_init operations")
r_require_match_count(async_mir_output
    "sync_contract=[(]const_borrow [(]struct \"test[.]codegen[.]async_sync_checked_retry\"::\"move_value\"[)][)]"
    3 "async MIR public Move-value borrows")
r_require_match_count(async_mir_output
    "callback_storage=[(]carrier [(]struct \"test[.]codegen[.]async_sync_checked_retry\"::\"move_value\"[)]"
    3 "async MIR hidden Move-value callback carriers")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.sync_checked_retry::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SYNC_SOURCE_FILE}"
    RESULT_VARIABLE sync_c17_result
    OUTPUT_VARIABLE sync_c17_output
    ERROR_VARIABLE sync_c17_error
)
if(NOT sync_c17_result EQUAL 0)
    message(FATAL_ERROR "sync checked-retry C17 emit failed (${sync_c17_result}): ${sync_c17_error}")
endif()
r_require_match_count(sync_c17_output "#include \"r_std_sync[.]h\"" 1
    "generated std.sync include")
r_require_match_count(sync_c17_output "r_std_sync_call_once[(]" 6
    "runtime call_once submissions")
r_require_match_count(sync_c17_output "r_std_sync_call_once_force[(]" 3
    "runtime call_once_force submissions")
r_require_match_count(sync_c17_output "r_std_sync_get_or_init[(]" 7
    "runtime get_or_init submissions")
r_require_match_count(sync_c17_output "static _Bool r_sync_initializer_[0-9]+[(]" 10
    "one generated callback thunk per reachable initializer")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_sync_checked_retry::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE async_c17_result
    OUTPUT_VARIABLE async_c17_output
    ERROR_VARIABLE async_c17_error
)
if(NOT async_c17_result EQUAL 0)
    message(FATAL_ERROR
        "async checked-retry C17 emit failed (${async_c17_result}): ${async_c17_error}")
endif()
r_require_match_count(async_c17_output "#include \"r_std_sync[.]h\"" 1
    "generated async std.sync include")
r_require_match_count(async_c17_output "r_std_sync_call_once[(]" 2
    "async runtime call_once submissions")
r_require_match_count(async_c17_output "r_std_sync_get_or_init[(]" 3
    "async runtime get_or_init submissions")
r_require_match_count(async_c17_output "static _Bool r_sync_initializer_[0-9]+[(]" 4
    "async callback thunks")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_sync_checked_retry::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async checked-retry C17 emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT async_c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "async checked-retry C17 output is not deterministic")
endif()
