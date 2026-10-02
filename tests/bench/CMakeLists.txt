# M5 benchmark pairs: each R program is compiled through r-front and the codegen program
# pipeline at -O2 (COMPILE_ONLY), each C mirror with the same compiler and flags.

set(R_BENCH_CHECK_SCRIPT "${PROJECT_SOURCE_DIR}/tests/check_codegen_program.cmake")
set(R_BENCH_TARGET_MANIFEST
    "${PROJECT_SOURCE_DIR}/targets/arm64-apple-darwin.hosted-native-async.json")
set(R_BENCH_PAIRS baseline call_overhead index_fixed index_slice checked_arith
    baseline_async async_task_start async_scoped_start async_fs_read_into array_push format_int
    own_alloc array_get string_append dict_lookup json_parse deflate_roundtrip tcp_echo)
set(R_BENCH_ITERATIONS
    "call_overhead=200000000"
    "index_fixed=204800000"
    "index_slice=204800000"
    "checked_arith=200000000"
    "async_task_start=2000000"
    "async_scoped_start=2000000"
    "async_fs_read_into=20000"
    "array_push=50000000"
    "format_int=5000000"
    "own_alloc=20000000"
    "array_get=200000000"
    "string_append=20000000"
    "dict_lookup=50000000"
    "json_parse=200000"
    "deflate_roundtrip=200"
    "tcp_echo=20000")
set(R_BENCH_C_OPTIONS
    -O2 -std=c17 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wsign-conversion
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes)
set(R_BENCH_LIBRARY_TARGETS
    r_core r_std_alloc r_std_arc r_std_array r_std_list r_std_dict r_std_bits r_std_bytes
    r_std_secret r_std_random r_std_hash r_std_math r_std_format r_std_json r_std_net r_std_process r_std_signal
    r_std_test
    r_std_string r_std_utf8 r_std_async r_std_c r_std_convert r_std_env r_std_error r_std_fs
    r_std_io r_std_rc r_std_sync r_std_thread r_std_time r_runtime_darwin_socket
    r_runtime_darwin_dns r_runtime_darwin_io r_runtime_darwin_process r_runtime_darwin_event
    r_runtime_darwin_timer)

# Experiment hook: one extra flag for the final compile+link of each generated program
# (for example -flto together with archives built with -flto). Empty in normal builds.
set(R_BENCH_GENERATED_C_EXTRA_FLAG "" CACHE STRING
    "Extra flag appended to the generated-program compile/link of the benchmark pairs")
set(R_BENCH_CODEGEN_ARGUMENTS
    -DGENERATED_C_EXTRA_FLAGS=${R_BENCH_GENERATED_C_EXTRA_FLAG}
    -DR_FRONT_EXECUTABLE=$<TARGET_FILE:r-front>
    -DC_COMPILER=${CMAKE_C_COMPILER}
    -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
    -DTARGET_TOOLCHAIN_CHECK=${PROJECT_SOURCE_DIR}/tools/check_target_toolchain.py
    -DTARGET_MANIFEST=${R_BENCH_TARGET_MANIFEST}
    -DSOURCE_2=
    -DLIBRARY_MAP=${PROJECT_SOURCE_DIR}/library/r/library.map
    -DLINK_MANIFEST=
    -DEXTRA_C_SOURCES=
    -DABI_VERIFY=OFF
    -DABI_INVENTORY=OFF
    -DABI_RECORD_TOOL=${PROJECT_SOURCE_DIR}/tools/generate_c_abi_record.py
    -DHEADER_INCLUDE_DIRS=
    -DHASH_ONLY=OFF
    -DC_WRAPPER=
    -DRUNTIME_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/include
    -DRUNTIME_DARWIN_INCLUDE=${PROJECT_SOURCE_DIR}/runtime/darwin/include
    -DR_STD_ALLOC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/alloc/include
    -DR_STD_ASYNC_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/async/include
    -DR_STD_C_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/c/include
    -DR_STD_CONVERT_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/convert/include
    -DR_STD_ERROR_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/error/include
    -DR_STD_STRING_INCLUDE=${PROJECT_SOURCE_DIR}/library/std/string/include
    -DR_LIBRARY_ROOT=${PROJECT_SOURCE_DIR}/library
    -DR_STD_ALLOC_LIBRARY=$<TARGET_FILE:r_std_alloc>
    -DR_STD_ARC_LIBRARY=$<TARGET_FILE:r_std_arc>
    -DR_STD_ARRAY_LIBRARY=$<TARGET_FILE:r_std_array>
    -DR_STD_LIST_LIBRARY=$<TARGET_FILE:r_std_list>
    -DR_STD_DICT_LIBRARY=$<TARGET_FILE:r_std_dict>
    -DR_STD_BITS_LIBRARY=$<TARGET_FILE:r_std_bits>
    -DR_STD_BYTES_LIBRARY=$<TARGET_FILE:r_std_bytes>
    -DR_STD_SECRET_LIBRARY=$<TARGET_FILE:r_std_secret>
    -DR_STD_RANDOM_LIBRARY=$<TARGET_FILE:r_std_random>
    -DR_STD_SIGNAL_LIBRARY=$<TARGET_FILE:r_std_signal>
    -DR_STD_TEST_LIBRARY=$<TARGET_FILE:r_std_test>
    "-DSTD_NATIVE_INCLUDE_DIRS=${R_STD_TLS_NATIVE_INCLUDE_DIR}"
    "-DSTD_NATIVE_LIBRARIES=${R_STD_TLS_NATIVE_LINK_ARCHIVES}"
    -DR_STD_HASH_LIBRARY=$<TARGET_FILE:r_std_hash>
    -DR_STD_MATH_LIBRARY=$<TARGET_FILE:r_std_math>
    -DR_STD_FORMAT_LIBRARY=$<TARGET_FILE:r_std_format>
    -DR_STD_JSON_LIBRARY=$<TARGET_FILE:r_std_json>
    -DR_STD_NET_LIBRARY=$<TARGET_FILE:r_std_net>
    -DR_STD_PROCESS_LIBRARY=$<TARGET_FILE:r_std_process>
    -DRUNTIME_DARWIN_SOCKET_LIBRARY=$<TARGET_FILE:r_runtime_darwin_socket>
    -DRUNTIME_DARWIN_DNS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_dns>
    -DR_STD_STRING_LIBRARY=$<TARGET_FILE:r_std_string>
    -DR_STD_UTF8_LIBRARY=$<TARGET_FILE:r_std_utf8>
    -DR_CORE_LIBRARY=$<TARGET_FILE:r_core>
    -DR_STD_ASYNC_LIBRARY=$<TARGET_FILE:r_std_async>
    -DR_STD_C_LIBRARY=$<TARGET_FILE:r_std_c>
    -DR_STD_CONVERT_LIBRARY=$<TARGET_FILE:r_std_convert>
    -DR_STD_ENV_LIBRARY=$<TARGET_FILE:r_std_env>
    -DR_STD_ERROR_LIBRARY=$<TARGET_FILE:r_std_error>
    -DR_STD_FS_LIBRARY=$<TARGET_FILE:r_std_fs>
    -DR_STD_IO_LIBRARY=$<TARGET_FILE:r_std_io>
    -DR_STD_RC_LIBRARY=$<TARGET_FILE:r_std_rc>
    -DR_STD_SYNC_LIBRARY=$<TARGET_FILE:r_std_sync>
    -DR_STD_THREAD_LIBRARY=$<TARGET_FILE:r_std_thread>
    -DR_STD_TIME_LIBRARY=$<TARGET_FILE:r_std_time>
    -DRUNTIME_HOSTED_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/hosted_entry.c
    -DRUNTIME_ALLOCATOR_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/allocator.c
    -DRUNTIME_ARRAY_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/array.c
    -DRUNTIME_LIST_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/list.c
    -DRUNTIME_DICT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/dict.c
    -DRUNTIME_ARC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/arc.c
    -DRUNTIME_RC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/rc.c
    -DRUNTIME_DROP_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/drop.c
    -DRUNTIME_OWN_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/own.c
    -DRUNTIME_THREAD_ATTACHMENT_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/thread_attachment.c
    -DRUNTIME_TASK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/task.c
    -DRUNTIME_STACK_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/stack.c
    -DRUNTIME_DARWIN_IO_LIBRARY=$<TARGET_FILE:r_runtime_darwin_io>
    -DRUNTIME_DARWIN_PROCESS_LIBRARY=$<TARGET_FILE:r_runtime_darwin_process>
    -DRUNTIME_DARWIN_EVENT_LIBRARY=$<TARGET_FILE:r_runtime_darwin_event>
    -DRUNTIME_DARWIN_TIMER_LIBRARY=$<TARGET_FILE:r_runtime_darwin_timer>
    -DRUNTIME_FS_LANE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane.c
    -DRUNTIME_FS_LANE_ASYNC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_async.c
    -DRUNTIME_FS_LANE_SYSCALLS_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_lane_syscalls.c
    -DRUNTIME_FS_SERVICE_SOURCE=${PROJECT_SOURCE_DIR}/runtime/darwin/source/fs_service.c
    -DRUNTIME_STRING_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/string.c
    -DRUNTIME_UTF8_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/utf8.c
    -DRUNTIME_PANIC_SOURCE=${PROJECT_SOURCE_DIR}/runtime/source/panic_abort.c
    -DEXPECTED_PANIC=
    -DEXPECTED_EXIT_STATUS=0
    -DPROGRAM_ARGUMENTS=
    -DPROGRAM_INPUT_FILE=
    -DCHECK_STDOUT=FALSE
    -DEXPECTED_STDOUT=
    -DCREATE_DIRECTORY_RELATIVE=
    -DATOMIC_WRITE_SOURCE=
    -DATOMIC_WRITE_REPLACEMENT_SOURCE=
    -DATOMIC_WRITE_RELATIVE=
    -DMEASURE_GENERATED_STACK_USAGE=TRUE
    -DSTACK_USAGE_MAX_FRAME_SIZE=262144
    -DSTACK_USAGE_EXPECT_FRAME_GREATER_THAN=
    -DENABLE_SANITIZERS=${R_FRONTEND_ENABLE_SANITIZERS}
    -DENABLE_THREAD_SANITIZER=${R_FRONTEND_ENABLE_THREAD_SANITIZER}
    -DGENERATED_C_OPTIMIZATION=-O2
    -DCOMPILE_ONLY=ON)

set(R_BENCH_PROGRAM_OUTPUTS)
set(R_BENCH_PAIR_ARGUMENTS)
set(R_BENCH_PAIR_SPECIFICATIONS)
foreach(R_BENCH_NAME IN LISTS R_BENCH_PAIRS)
    add_executable(bench_${R_BENCH_NAME}_c "${CMAKE_CURRENT_SOURCE_DIR}/${R_BENCH_NAME}.c")
    target_compile_options(bench_${R_BENCH_NAME}_c PRIVATE ${R_BENCH_C_OPTIONS})
    if(R_BENCH_NAME STREQUAL "deflate_roundtrip")
        target_link_libraries(bench_${R_BENCH_NAME}_c PRIVATE z)
    endif()
    set(R_BENCH_R_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/bench_${R_BENCH_NAME}_r")
    add_custom_command(
        OUTPUT "${R_BENCH_R_EXECUTABLE}"
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_1=${CMAKE_CURRENT_SOURCE_DIR}/${R_BENCH_NAME}.r
            -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/bench_${R_BENCH_NAME}_r.c
            -DOUTPUT_EXE=${R_BENCH_R_EXECUTABLE}
            ${R_BENCH_CODEGEN_ARGUMENTS}
            -P "${R_BENCH_CHECK_SCRIPT}"
        DEPENDS
            r-front
            ${R_BENCH_LIBRARY_TARGETS}
            "${CMAKE_CURRENT_SOURCE_DIR}/${R_BENCH_NAME}.r"
            "${R_BENCH_CHECK_SCRIPT}"
            "${PROJECT_SOURCE_DIR}/tests/check_codegen_stack_usage.cmake"
        COMMENT "Compiling benchmark ${R_BENCH_NAME}.r through r-front at -O2"
        VERBATIM
    )
    list(APPEND R_BENCH_PROGRAM_OUTPUTS "${R_BENCH_R_EXECUTABLE}")
    list(APPEND R_BENCH_PAIR_ARGUMENTS
        --pair "${R_BENCH_NAME}=${R_BENCH_R_EXECUTABLE}:$<TARGET_FILE:bench_${R_BENCH_NAME}_c>")
    list(APPEND R_BENCH_PAIR_SPECIFICATIONS
        "${R_BENCH_NAME}=${R_BENCH_R_EXECUTABLE}:$<TARGET_FILE:bench_${R_BENCH_NAME}_c>")
endforeach()

set(R_BENCH_ITERATION_ARGUMENTS)
foreach(R_BENCH_ITERATION IN LISTS R_BENCH_ITERATIONS)
    list(APPEND R_BENCH_ITERATION_ARGUMENTS --iterations "${R_BENCH_ITERATION}")
endforeach()
set(R_BENCH_C_TARGETS)
foreach(R_BENCH_NAME IN LISTS R_BENCH_PAIRS)
    list(APPEND R_BENCH_C_TARGETS bench_${R_BENCH_NAME}_c)
endforeach()

string(TOUPPER "${CMAKE_BUILD_TYPE}" R_BENCH_BUILD_TYPE_UPPER)
add_custom_target(bench_programs DEPENDS ${R_BENCH_PROGRAM_OUTPUTS} ${R_BENCH_C_TARGETS})
add_custom_target(benchmarks
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_benchmarks.py"
        --front $<TARGET_FILE:r-front>
        --cc "${CMAKE_C_COMPILER}"
        --target-manifest "${R_BENCH_TARGET_MANIFEST}"
        --runs 7
        --build-type "${CMAKE_BUILD_TYPE}"
        --runtime-c-flags "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${R_BENCH_BUILD_TYPE_UPPER}}"
        --output "${PROJECT_SOURCE_DIR}/docs/benchmarks.md"
        --json "${CMAKE_CURRENT_BINARY_DIR}/benchmarks.json"
        ${R_BENCH_ITERATION_ARGUMENTS}
        ${R_BENCH_PAIR_ARGUMENTS}
    DEPENDS bench_programs
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Timing the R/C benchmark pairs and writing docs/benchmarks.md"
    VERBATIM
)

string(REPLACE ";" "|" R_BENCH_PAIRS_JOINED "${R_BENCH_PAIR_SPECIFICATIONS}")
add_test(
    NAME r_bench_pairs_agree
    COMMAND "${CMAKE_COMMAND}"
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DPYTHON=${Python3_EXECUTABLE}
        -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/run_benchmarks.py
        "-DPAIRS=${R_BENCH_PAIRS_JOINED}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_bench_pairs.cmake"
)
set_tests_properties(r_bench_pairs_agree PROPERTIES LABELS "bench" TIMEOUT 600 RUN_SERIAL TRUE)
