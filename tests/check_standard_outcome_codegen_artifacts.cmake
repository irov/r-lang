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

# --all-functions: the inspected functions are not called by main.
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --all-functions
        "${SYNC_SOURCE_FILE}"
        "${MAIN_SOURCE_FILE}"
    RESULT_VARIABLE sync_status
    OUTPUT_VARIABLE sync_output
    ERROR_VARIABLE sync_error
)
if(NOT sync_status EQUAL 0)
    message(FATAL_ERROR "sync outcome LLVM IR emit failed (${sync_status}): ${sync_error}")
endif()
# The outcome values move through the library's own entries.
r_require_match(sync_output "call [^\n]*@(r_shim_)?r_std_io_write_all_result_move_initialize[(]"
    "sync IO typed moves")
r_require_match(sync_output "call [^\n]*@(r_shim_)?r_std_fs_write_file_result_move_initialize[(]"
    "sync filesystem typed moves")
# inspect_fs compares the error code with the ABI value of std.fs::error_code::already_exists.
string(FIND "${sync_output}" "define internal i32 @\"semantic.standard_outcomes::inspect_fs\"("
    inspect_fs_start)
if(inspect_fs_start EQUAL -1)
    message(FATAL_ERROR "sync outcome LLVM IR lacks inspect_fs:\n${sync_output}")
endif()
string(SUBSTRING "${sync_output}" ${inspect_fs_start} -1 inspect_fs_body)
string(FIND "${inspect_fs_body}" "\n}\n" inspect_fs_end)
string(SUBSTRING "${inspect_fs_body}" 0 ${inspect_fs_end} inspect_fs_body)
r_require_match_count(inspect_fs_body "icmp eq i32 %[0-9]+, 4\n" 1
    "filesystem already_exists ABI value")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --all-functions
        --entry test.codegen.async_standard_outcomes::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${ASYNC_SOURCE_FILE}"
    RESULT_VARIABLE async_status
    OUTPUT_VARIABLE async_output
    ERROR_VARIABLE async_error
)
if(NOT async_status EQUAL 0)
    message(FATAL_ERROR "async outcome LLVM IR emit failed (${async_status}): ${async_error}")
endif()
r_require_match(async_output
    "call [^\n]*@(r_shim_)?r_std_io_write_all_result_move_initialize[(]"
    "async IO move glue")
r_require_match(async_output "call [^\n]*@(r_shim_)?r_std_io_write_all_result_destroy[(]"
    "async IO drop glue")
