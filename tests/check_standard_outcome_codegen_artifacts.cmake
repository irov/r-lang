if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SYNC_SOURCE_FILE OR
   NOT DEFINED MAIN_SOURCE_FILE OR
   NOT DEFINED ASYNC_SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SYNC_SOURCE_FILE, MAIN_SOURCE_FILE, ASYNC_SOURCE_FILE and "
        "TARGET_MANIFEST are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

function(r_require_match variable pattern description)
    string(REGEX MATCH "${pattern}" match "${${variable}}")
    if(match STREQUAL "")
        message(FATAL_ERROR "${description}: pattern not found:\n${${variable}}")
    endif()
endfunction()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        "${SYNC_SOURCE_FILE}"
        "${MAIN_SOURCE_FILE}"
    RESULT_VARIABLE sync_status
    OUTPUT_VARIABLE sync_output
    ERROR_VARIABLE sync_error
)
if(NOT sync_status EQUAL 0)
    message(FATAL_ERROR "sync outcome C17 emit failed (${sync_status}): ${sync_error}")
endif()
r_require_match_count(sync_output "R_STD_IO_WRITE_ALL_RESULT_WRITTEN" 1
    "sync IO written switch tag")
r_require_match_count(sync_output "R_STD_IO_WRITE_ALL_RESULT_FAILED" 1
    "sync IO failed switch tag")
r_require_match_count(sync_output "R_STD_FS_WRITE_FILE_COMMITTED" 1
    "sync filesystem committed switch tag")
r_require_match_count(sync_output "R_STD_FS_WRITE_FILE_FAILED" 1
    "sync filesystem failed switch tag")
r_require_match_count(sync_output "R_STD_FS_WRITE_FILE_RESULT_" 0
    "non-ABI filesystem tag names")
r_require_match_count(sync_output "r_std_io_write_all_result_move_initialize" 1
    "sync IO typed moves")
r_require_match_count(sync_output "r_std_fs_write_file_result_move_initialize" 1
    "sync filesystem typed moves")
r_require_match_count(sync_output
    "static inline void r_type_move_s[0-9]+[(]" 4
    "sync standard-outcome move adapter declarations and definitions")
r_require_match_count(sync_output
    "static inline void r_type_move_s[0-9]+_gate[(]" 4
    "sync standard-outcome move gate declarations and definitions")
r_require_match_count(sync_output
    "R_STACK_FRAME[(]r_type_move_s[0-9]+[)]" 0
    "sync standard-outcome move gates carry no preflight (R-FUNC-0004)")
r_require_match_count(sync_output
    "static inline void r_type_drop_s[0-9]+" 0
    "unused sync standard-outcome drop adapters")
r_require_match_count(sync_output "_source = [{]0[}]" 2
    "sync synthesized failure payload sources")
r_require_match_count(sync_output
    "_source[.]r_m[0-9]+ = r_t[0-9]+[.]error" 2
    "sync flattened native errors")
r_require_match_count(sync_output
    "_source[.]r_m[0-9]+ = r_t[0-9]+[.]written" 1
    "sync flattened native progress")
r_require_match_count(sync_output "[(]RStdFsErrorCode[)]INT32_C[(]4[)]" 1
    "filesystem already_exists ABI value")
r_require_match_count(sync_output "= [(]RStdIoWriteAllResult[)][{]0[}]" 2
    "sync IO native-owner clears")
r_require_match_count(sync_output "= [(]RStdFsWriteFileResult[)][{]0[}]" 2
    "sync filesystem native-owner clears")
r_require_match_count(sync_output "R_INTERNAL_ASSERT[(]" 0
    "sync generated C omits assertions")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_standard_outcomes::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE async_status
    OUTPUT_VARIABLE async_output
    ERROR_VARIABLE async_error
)
if(NOT async_status EQUAL 0)
    message(FATAL_ERROR "async outcome C17 emit failed (${async_status}): ${async_error}")
endif()
r_require_match_count(async_output "R_STD_IO_WRITE_ALL_RESULT_WRITTEN" 0
    "async C omits assertion-only written-tag references")
r_require_match_count(async_output "R_STD_IO_WRITE_ALL_RESULT_FAILED" 0
    "async C omits assertion-only failed-tag references")
r_require_match_count(async_output
    "frame->r_v00000001 = [(]uint32_t[)]frame->r_v00000000[.]kind" 1
    "async C directly discriminates the write-all outcome")
r_require_match_count(async_output "R_INTERNAL_ASSERT[(]" 0
    "async generated C omits assertions")
r_require_match_count(async_output "r_std_io_write_all_result_move_initialize" 1
    "async IO move adapter")
r_require_match_count(async_output "r_std_io_write_all_result_destroy" 1
    "async IO drop adapter")
r_require_match_count(async_output
    "static inline void r_type_move_s[0-9]+[(]" 2
    "async standard-outcome move adapter declaration and definition")
r_require_match_count(async_output
    "static inline void r_type_move_s[0-9]+_gate[(]" 2
    "async standard-outcome move gate declaration and definition")
r_require_match_count(async_output
    "static inline void r_type_drop_s[0-9]+[(]" 2
    "async standard-outcome drop adapter declaration and definition")
r_require_match_count(async_output
    "static inline void r_type_drop_s[0-9]+_gate[(]" 2
    "async standard-outcome drop gate declaration and definition")
r_require_match_count(async_output
    "R_STACK_FRAME[(]r_type_(move|drop)_s[0-9]+[)]" 0
    "async standard-outcome glue gates carry no preflight (R-FUNC-0004)")
r_require_match_count(async_output
    "frame->r_v[0-9]+[.]r_m[0-9]+ = frame->r_v[0-9]+[.]error" 1
    "async flattened native errors")
r_require_match_count(async_output
    "frame->r_v[0-9]+[.]r_m[0-9]+ = frame->r_v[0-9]+[.]written" 1
    "async flattened native progress")
r_require_match_count(async_output "= [(]RStdIoWriteAllResult[)][{]0[}]" 2
    "async IO native-owner clears")
r_require_match(async_output
    "frame->r_v[0-9]+_initialized = 1;[\n ]+frame->r_v[0-9]+_initialized = 0;"
    "async payload/source initialization transfers")
